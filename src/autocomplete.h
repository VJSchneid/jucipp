#ifndef JUCI_AUTOCOMPLETE_H_
#define JUCI_AUTOCOMPLETE_H_
#include "dispatcher.h"
#include "tooltips.h"
#include <atomic>
#include <thread>

class Autocomplete {
  Gtk::TextView *view;
  bool &interactive_completion;
  /// Some libraries/utilities, like libclang, require that autocomplete is started at the beginning of a word
  bool strip_word;

  Dispatcher dispatcher;

public:
  enum class State { IDLE, STARTING, RESTARTING, CANCELED };

  std::string prefix;
  std::mutex prefix_mutex;
  std::vector<std::pair<std::string, std::string>> rows;
  Tooltips tooltips;

  std::atomic<State> state;

  std::thread thread;

  std::function<bool()> is_processing = [] { return true; };
  std::function<void()> reparse = [] {};
  std::function<void()> cancel_reparse = [] {};
  std::function<std::unique_ptr<std::lock_guard<std::mutex>>()> get_parse_lock = [] { return nullptr; };
  std::function<void()> stop_parse = [] {};

  std::function<bool(guint last_keyval)> is_continue_key = [](guint) { return false; };
  std::function<bool(guint last_keyval)> is_restart_key = [](guint) { return false; };
  std::function<bool()> run_check = [] { return false; };

  std::function<void()> before_add_rows = [] {};
  std::function<void()> after_add_rows = [] {};
  std::function<void()> on_add_rows_error = [] {};

  /// The handler is not run in the main loop.
  std::function<void(std::string &buffer, int line_number, int column)> add_rows = [](std::string &, int, int) {};

  std::function<void()> setup_dialog = [] {};

  Autocomplete(Gtk::TextView *view, bool &interactive_completion, guint &last_keyval, bool strip_word);

  void run();
  void stop();

private:
  void setup_initial_dialog();
};

#endif // JUCI_AUTOCOMPLETE_H_
