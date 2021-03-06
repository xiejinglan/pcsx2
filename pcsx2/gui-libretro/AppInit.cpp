/*  PCSX2 - PS2 Emulator for PCs
 *  Copyright (C) 2002-2010  PCSX2 Dev Team
 *
 *  PCSX2 is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU Lesser General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  PCSX2 is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with PCSX2.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

#include "PrecompiledHeader.h"
#include "ConsoleLogger.h"
#include "MTVU.h" // for thread cancellation on shutdown

#include "Utilities/IniInterface.h"

#include <wx/cmdline.h>
#include <wx/intl.h>
#include <wx/stdpaths.h>
#include <memory>
void Pcsx2App::DetectCpuAndUserMode()
{
	AffinityAssert_AllowFrom_MainUI();
	
#ifdef _M_X86
	x86caps.Identify();
	x86caps.CountCores();
	x86caps.SIMD_EstablishMXCSRmask();

	if(!x86caps.hasStreamingSIMD2Extensions )
	{
		// This code will probably never run if the binary was correctly compiled for SSE2
		// SSE2 is required for any decent speed and is supported by more than decade old x86 CPUs
		throw Exception::HardwareDeficiency()
			.SetDiagMsg(L"Critical Failure: SSE2 Extensions not available.")
			.SetUserMsg(_("SSE2 extensions are not available.  PCSX2 requires a cpu that supports the SSE2 instruction set."));
	}
#endif

	EstablishAppUserMode();

	// force unload plugins loaded by the wizard.  If we don't do this the recompilers might
	// fail to allocate the memory they need to function.
#ifdef __LIBRETRO__
	CoreThread.Cancel();
	CorePlugins.Shutdown();
	CorePlugins.Unload();
#else
	ShutdownPlugins();
	UnloadPlugins();
#endif
}

void Pcsx2App::AllocateCoreStuffs()
{
	if( AppRpc_TryInvokeAsync( &Pcsx2App::AllocateCoreStuffs ) ) return;

	SysLogMachineCaps();
	AppApplySettings();

	GetVmReserve().ReserveAll();

	if( !m_CpuProviders )
	{
		// FIXME : Some or all of SysCpuProviderPack should be run from the SysExecutor thread,
		// so that the thread is safely blocked from being able to start emulation.

		m_CpuProviders = std::make_unique<SysCpuProviderPack>();

		if( m_CpuProviders->HadSomeFailures( g_Conf->EmuOptions.Cpu.Recompiler ) )
		{
			// HadSomeFailures only returns 'true' if an *enabled* cpu type fails to init.  If
			// the user already has all interps configured, for example, then no point in
			// popping up this dialog.
			Pcsx2Config::RecompilerOptions& recOps = g_Conf->EmuOptions.Cpu.Recompiler;
			
			if( BaseException* ex = m_CpuProviders->GetException_EE() )
			{
				recOps.EnableEE		= false;
			}

			if( BaseException* ex = m_CpuProviders->GetException_IOP() )
			{
				recOps.EnableIOP	= false;
			}

			if( BaseException* ex = m_CpuProviders->GetException_MicroVU0() )
			{
				recOps.UseMicroVU0	= false;
				recOps.EnableVU0	= false;
			}

			if( BaseException* ex = m_CpuProviders->GetException_MicroVU1() )
			{
				recOps.UseMicroVU1	= false;
				recOps.EnableVU1	= false;
			}
		}
	}

#ifdef __LIBRETRO__
	LoadPluginsImmediate();
#else
	LoadPluginsPassive();
#endif
}

typedef void (wxEvtHandler::*pxInvokeAppMethodEventFunction)(Pcsx2AppMethodEvent&);
typedef void (wxEvtHandler::*pxStuckThreadEventHandler)(pxMessageBoxEvent&);

// --------------------------------------------------------------------------------------
//   GameDatabaseLoaderThread
// --------------------------------------------------------------------------------------
class GameDatabaseLoaderThread : public pxThread
	, EventListener_AppStatus
{
	typedef pxThread _parent;

public:
	GameDatabaseLoaderThread()
		: pxThread( L"GameDatabaseLoader" )
	{
	}

	virtual ~GameDatabaseLoaderThread()
	{
		try {
			_parent::Cancel();
		}
		DESTRUCTOR_CATCHALL
	}

protected:
	void ExecuteTaskInThread()
	{
		Sleep(2);
		wxGetApp().GetGameDatabase();
	}

	void OnCleanupInThread()
	{
		_parent::OnCleanupInThread();
		wxGetApp().DeleteThread(this);
	}
	
	void AppStatusEvent_OnExit()
	{
		Block();
	}
};

bool Pcsx2App::OnInit()
{
    return true;
}

// This cleanup procedure can only be called when the App message pump is still active.
// OnExit() must use CleanupOnExit instead.
void Pcsx2App::CleanupRestartable()
{
	AffinityAssert_AllowFrom_MainUI();

	CoreThread.Cancel();
#ifndef __LIBRETRO__
	SysExecutorThread.ShutdownQueue();
#endif
}

// This cleanup handler can be called from OnExit (it doesn't need a running message pump),
// but should not be called from the App destructor.  It's needed because wxWidgets doesn't
// always call OnExit(), so I had to make CleanupRestartable, and then encapsulate it here
// to be friendly to the OnExit scenario (no message pump).
void Pcsx2App::CleanupOnExit()
{
	AffinityAssert_AllowFrom_MainUI();

	try
	{
		CleanupRestartable();
		CleanupResources();
	}
	catch( Exception::CancelEvent& )		{ throw; }
	catch( Exception::RuntimeError& ex )
	{
		// Handle runtime errors gracefully during shutdown.  Mostly these are things
		// that we just don't care about by now, and just want to "get 'er done!" so
		// we can exit the app. ;)

		Console.Error( L"Runtime exception handled during CleanupOnExit:\n" );
		Console.Indent().Error( ex.FormatDiagnosticMessage() );
	}

	// Notice: deleting the plugin manager (unloading plugins) here causes Lilypad to crash,
	// likely due to some pending message in the queue that references lilypad procs.
	// We don't need to unload plugins anyway tho -- shutdown is plenty safe enough for
	// closing out all the windows.  So just leave it be and let the plugins get unloaded
	// during the wxApp destructor. -- air
	
	// FIXME: performing a wxYield() here may fix that problem. -- air

	pxDoAssert = pxAssertImpl_LogIt;
	Console_SetActiveHandler( ConsoleWriter_Stdout );
}

void Pcsx2App::CleanupResources()
{
	//delete wxConfigBase::Set( NULL );

	while( wxGetLocale() != NULL )
		delete wxGetLocale();

	m_mtx_LoadingGameDB.Wait();
	ScopedLock lock(m_mtx_Resources);
	m_Resources = NULL;
}

int Pcsx2App::OnExit()
{
	CleanupOnExit();
	return wxApp::OnExit();
}
// --------------------------------------------------------------------------------------
//  SysEventHandler
// --------------------------------------------------------------------------------------
class SysEvtHandler : public pxEvtQueue
{
public:
	wxString GetEvtHandlerName() const { return L"SysExecutor"; }
};


Pcsx2App::Pcsx2App() 
{
	// Warning: Do not delete this comment block! Gettext will parse it to allow
	// the translation of some wxWidget internal strings. -- greg
	#if 0
	{
		// Some common labels provided by wxWidgets.  wxWidgets translation files are chucked full
		// of worthless crap, and tally more than 200k each.  We only need these couple.

		_("OK");
		_("&OK");
		_("Cancel");
		_("&Cancel");
		_("&Apply");
		_("&Next >");
		_("< &Back");
		_("&Back");
		_("&Finish");
		_("&Yes");
		_("&No");
		_("Browse");
		_("&Save");
		_("Save &As...");
		_("&Help");
		_("&Home");

		_("Show about dialog")
	}
	#endif

	m_PendingSaves			= 0;

	SetAppName( L"PCSX2" );
}

Pcsx2App::~Pcsx2App()
{
	pxDoAssert = pxAssertImpl_LogIt;	
	try {
		vu1Thread.Cancel();
	}
	DESTRUCTOR_CATCHALL
}

void Pcsx2App::CleanUp()
{
}

// ------------------------------------------------------------------------------------------
//  Using the MSVCRT to track memory leaks:
// ------------------------------------------------------------------------------------------
// When exiting PCSX2 normally, the CRT will make a list of all memory that's leaked.  The
// number inside {} can be pasted into the line below to cause MSVC to breakpoint on that
// allocation at the time it's made.  And then using a stacktrace you can figure out what
// leaked! :D
//
// Limitations: Unfortunately, wxWidgets gui uses a lot of heap allocations while handling
// messages, and so any mouse movements will pretty much screw up the leak value.  So to use
// this feature you need to execute pcsx in no-gui mode, and then not move the mouse or use
// the keyboard until you get to the leak. >_<
//
// (but this tool is still better than nothing!)

#ifdef PCSX2_DEBUG
struct CrtDebugBreak
{
	CrtDebugBreak( int spot )
	{
#ifdef __WXMSW__
		_CrtSetBreakAlloc( spot );
#endif
	}
};

//CrtDebugBreak breakAt( 11549 );

#endif
