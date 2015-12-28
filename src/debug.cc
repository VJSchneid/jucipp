#include "debug.h"

#include <thread>

#include "lldb/API/SBTarget.h"
#include "lldb/API/SBProcess.h"
#include "lldb/API/SBListener.h"
#include "lldb/API/SBEvent.h"
#include "lldb/API/SBBreakpoint.h"
#include "lldb/API/SBThread.h"
#include "lldb/API/SBStream.h"

#include <iostream> //TODO: remove
using namespace std;

void log(const char *msg, void *) {
  cout << "debugger log: " << msg << endl;
}

Debug::Debug(): stopped(false) {
  lldb::SBDebugger::Initialize();
  
  debugger=lldb::SBDebugger::Create(true, log, nullptr);
}

void Debug::start(const boost::filesystem::path &project_path, const boost::filesystem::path &executable,
                  const boost::filesystem::path &path, std::function<void(int exit_status)> callback) {
  std::thread debug_thread([this, project_path, executable, path, callback]() {
    auto target=debugger.CreateTarget(executable.string().c_str());
    auto listener=lldb::SBListener("juCi++ lldb listener");
    
    if(!target.IsValid()) {
      cerr << "Error: Could not create debug target to: " << executable << endl; //TODO: output to terminal instead
      return;
    }
    
    for(auto &breakpoint: breakpoints[project_path.string()]) {
      if(!(target.BreakpointCreateByLocation(breakpoint.first.c_str(), breakpoint.second)).IsValid()) {
        cerr << "Error: Could not create breakpoint at: " << breakpoint.first << ":" << breakpoint.second << endl; //TODO: output to terminal instead
        return;
      }
      else
        cerr << "Created breakpoint at: " << breakpoint.first << ":" << breakpoint.second << endl;
    }
    
    lldb::SBError error;
    process = std::unique_ptr<lldb::SBProcess>(new lldb::SBProcess(target.Launch(listener, nullptr, nullptr, nullptr, nullptr, nullptr, path.string().c_str(), lldb::eLaunchFlagNone, false, error)));
    
    if(error.Fail()) {
      cerr << "Error (debug): " << error.GetCString() << endl; //TODO: output to terminal instead
      return;
    }

    lldb::SBEvent event;
    while(true) {
      if(listener.WaitForEvent(3, event)) {
        auto state=process->GetStateFromEvent(event);
        bool expected=false;
        if(state==lldb::StateType::eStateStopped && stopped.compare_exchange_strong(expected, true)) {
          cout << "NumThreads: " << process->GetNumThreads() << endl;
          for(uint32_t thread_index_id=0;thread_index_id<process->GetNumThreads();thread_index_id++) {
            auto thread=process->GetThreadAtIndex(thread_index_id);
            cout << "NumFrames: " << thread.GetNumFrames() << endl;
            for(uint32_t frame_index=0;frame_index<thread.GetNumFrames();frame_index++) {
              auto frame=thread.GetFrameAtIndex(frame_index);
              auto values=frame.GetVariables(true, true, true, true);
              cout << "variables.GetSize(): " << values.GetSize() << endl;
              for(uint32_t value_index=0;value_index<values.GetSize();value_index++) {
                auto value=values.GetValueAtIndex(value_index);
                lldb::SBStream stream;
                value.GetDescription(stream);
                cout << stream.GetData();
              }
            }
          }
        }
        
        else if(state==lldb::StateType::eStateExited) {
          auto exit_status=process->GetExitStatus();
          if(callback)
            callback(exit_status);
          process.reset();
          stopped=false;
          return;
        }
        
        else if(state==lldb::StateType::eStateCrashed) {
          if(callback)
            callback(-1);
          process.reset();
          stopped=false;
          return;
        }
      }
      this_thread::sleep_for(std::chrono::milliseconds(200));
    }
  });
  debug_thread.detach();
}

void Debug::continue_debug() {
  bool expected=true;
  if(stopped.compare_exchange_strong(expected, false))
    process->Continue();
}
