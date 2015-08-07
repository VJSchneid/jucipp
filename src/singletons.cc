#include "singletons.h"

std::unique_ptr<Source::Config> Singleton::Config::source_=std::unique_ptr<Source::Config>(new Source::Config());
std::unique_ptr<Terminal::Config> Singleton::Config::terminal_=std::unique_ptr<Terminal::Config>(new Terminal::Config());
std::unique_ptr<Directories::Config> Singleton::Config::directories_=std::unique_ptr<Directories::Config>(new Directories::Config());
std::unique_ptr<Terminal> Singleton::terminal_=std::unique_ptr<Terminal>();
std::unique_ptr<Theme::Config> Singleton::Config::theme_ = std::unique_ptr<Theme::Config>(new Theme::Config());
std::unique_ptr<Window::Config> Singleton::Config::window_ = std::unique_ptr<Window::Config>(new Window::Config());


Terminal *Singleton::terminal() {
  if(!terminal_)
    terminal_=std::unique_ptr<Terminal>(new Terminal());
  return terminal_.get();
}
std::unique_ptr<Gtk::Label> Singleton::status_=std::unique_ptr<Gtk::Label>();
Gtk::Label *Singleton::status() {
  if(!status_)
    status_=std::unique_ptr<Gtk::Label>(new Gtk::Label());
  return status_.get();
}
