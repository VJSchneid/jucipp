#include "window.h"
#include "config.h"
#include "menu.h"
#include "notebooks.h"
#include "directories.h"
#include "dialogs.h"
#include "filesystem.h"
#include "project.h"
#include "entrybox.h"
#include "info.h"
#include "ctags.h"
#include "selection_dialog.h"

Window::Window() {
  set_title("juCi++");
  set_events(Gdk::POINTER_MOTION_MASK|Gdk::FOCUS_CHANGE_MASK|Gdk::SCROLL_MASK|Gdk::LEAVE_NOTIFY_MASK);
  
  set_menu_actions();
  configure();
  activate_menu_items();
  
  Menu::get().right_click_line_menu->attach_to_widget(*this);
  Menu::get().right_click_selected_menu->attach_to_widget(*this);

  set_default_size(Config::get().window.default_size.first, Config::get().window.default_size.second);
  
  auto directories_scrolled_window=Gtk::manage(new Gtk::ScrolledWindow());
  directories_scrolled_window->add(Directories::get());
  
  
  auto &notebook = Notebooks::get().create();
  auto notebook_vbox=Gtk::manage(new Gtk::Box(Gtk::Orientation::ORIENTATION_VERTICAL));
  notebook_vbox->pack_start(notebook);
  notebook_vbox->pack_end(EntryBox::get(), Gtk::PACK_SHRINK);
  
  auto terminal_scrolled_window=Gtk::manage(new Gtk::ScrolledWindow());
  terminal_scrolled_window->add(Terminal::get());
  
  auto notebook_and_terminal_vpaned=Gtk::manage(new Gtk::Paned(Gtk::Orientation::ORIENTATION_VERTICAL));
  notebook_and_terminal_vpaned->set_position(static_cast<int>(0.75*Config::get().window.default_size.second));
  notebook_and_terminal_vpaned->pack1(*notebook_vbox, Gtk::SHRINK);
  notebook_and_terminal_vpaned->pack2(*terminal_scrolled_window, Gtk::SHRINK);
  
  auto hpaned=Gtk::manage(new Gtk::Paned());
  hpaned->set_position(static_cast<int>(0.2*Config::get().window.default_size.first));
  hpaned->pack1(*directories_scrolled_window, Gtk::SHRINK);
  hpaned->pack2(*notebook_and_terminal_vpaned, Gtk::SHRINK);
  
  auto status_hbox=Gtk::manage(new Gtk::Box());
  status_hbox->set_homogeneous(true);
  status_hbox->pack_start(*Gtk::manage(new Gtk::Box()));
  auto status_right_hbox=Gtk::manage(new Gtk::Box());
  status_right_hbox->pack_end(notebook.status_state, Gtk::PACK_SHRINK);
  auto status_right_overlay=Gtk::manage(new Gtk::Overlay());
  status_right_overlay->add(*status_right_hbox);
  status_right_overlay->add_overlay(notebook.status_diagnostics);
  status_hbox->pack_end(*status_right_overlay);
  
  auto status_overlay=Gtk::manage(new Gtk::Overlay());
  status_overlay->add(*status_hbox);
  auto status_file_info_hbox=Gtk::manage(new Gtk::Box);
  status_file_info_hbox->pack_start(notebook.status_file_path, Gtk::PACK_SHRINK);
  status_file_info_hbox->pack_start(notebook.status_branch, Gtk::PACK_SHRINK);
  status_file_info_hbox->pack_start(notebook.status_location, Gtk::PACK_SHRINK);
  status_overlay->add_overlay(*status_file_info_hbox);
  status_overlay->add_overlay(Project::debug_status_label());
  
  auto vbox=Gtk::manage(new Gtk::Box(Gtk::Orientation::ORIENTATION_VERTICAL));
  vbox->pack_start(*hpaned);
  vbox->pack_start(*status_overlay, Gtk::PACK_SHRINK);
  
  auto overlay_vbox=Gtk::manage(new Gtk::Box(Gtk::Orientation::ORIENTATION_VERTICAL));
  auto overlay_hbox=Gtk::manage(new Gtk::Box());
  overlay_vbox->set_hexpand(false);
  overlay_vbox->set_halign(Gtk::Align::ALIGN_START);
  overlay_vbox->pack_start(Info::get(), Gtk::PACK_SHRINK, 20);
  overlay_hbox->set_hexpand(false);
  overlay_hbox->set_halign(Gtk::Align::ALIGN_END);
  overlay_hbox->pack_end(*overlay_vbox, Gtk::PACK_SHRINK, 20);
  
  auto overlay=Gtk::manage(new Gtk::Overlay());
  overlay->add(*vbox);
  overlay->add_overlay(*overlay_hbox);
  overlay->set_overlay_pass_through(*overlay_hbox, true);
  add(*overlay);
  
  show_all_children();
  Info::get().hide();

  //Scroll to end of terminal whenever info is printed
  Terminal::get().signal_size_allocate().connect([terminal_scrolled_window](Gtk::Allocation& allocation){
    auto adjustment=terminal_scrolled_window->get_vadjustment();
    adjustment->set_value(adjustment->get_upper()-adjustment->get_page_size());
    Terminal::get().queue_draw();
  });

  EntryBox::get().signal_show().connect([this, hpaned, notebook_and_terminal_vpaned, notebook_vbox](){
    hpaned->set_focus_chain({notebook_and_terminal_vpaned});
    notebook_and_terminal_vpaned->set_focus_chain({notebook_vbox});
    notebook_vbox->set_focus_chain({&EntryBox::get()});
  });
  EntryBox::get().signal_hide().connect([this, hpaned, notebook_and_terminal_vpaned, notebook_vbox](){
    hpaned->unset_focus_chain();
    notebook_and_terminal_vpaned->unset_focus_chain();
    notebook_vbox->unset_focus_chain();
  });
  EntryBox::get().signal_hide().connect([this]() {
    if(auto notebook=Notebooks::get().get_current_notebook()) {
       if (auto view=notebook->get_current_view()) {
         view->grab_focus();
       }
     }
  });

  Notebooks::get().on_change_page=[this](NotebookPaned *notebook, Source::View *view) {
    if(search_entry_shown && EntryBox::get().labels.size()>0) {
      view->update_search_occurrences=[this](int number){
        EntryBox::get().labels.begin()->update(0, std::to_string(number));
      };
      view->search_highlight(last_search, case_sensitive_search, regex_search);
    }

    activate_menu_items();
    
    Directories::get().select(view->file_path);
    
    if(view->full_reparse_needed)
      view->full_reparse();
    else if(view->soft_reparse_needed)
      view->soft_reparse();
    
    notebook->update_status(view);
    
#ifdef JUCI_ENABLE_DEBUG
    if(Project::debugging)
      Project::debug_update_stop();
#endif
  };
  Notebooks::get().on_close_page=[this](NotebookPaned *notebook, Source::View *view) {
#ifdef JUCI_ENABLE_DEBUG
    if(Project::current && Project::debugging) {
      auto iter=view->get_buffer()->begin();
      while(view->get_source_buffer()->forward_iter_to_source_mark(iter, "debug_breakpoint") ||
         view->get_source_buffer()->get_source_marks_at_iter(iter, "debug_breakpoint").size()) {
        auto end_iter=view->get_iter_at_line_end(iter.get_line());
        view->get_source_buffer()->remove_source_marks(iter, end_iter, "debug_breakpoint");
        Project::current->debug_remove_breakpoint(view->file_path, iter.get_line()+1, view->get_buffer()->get_line_count()+1);
      }
    }
#endif
    EntryBox::get().hide();
    if (auto notebook=Notebooks::get().get_current_notebook()) {
      if(auto view=notebook->get_current_view())
        notebook->update_status(view);
      else {
        notebook->clear_status();
        
        activate_menu_items();
      }
    }
  };
  
  signal_focus_out_event().connect([](GdkEventFocus *event) {
    if(auto notebook=Notebooks::get().get_current_notebook()) {
      if(auto view=notebook->get_current_view()) {
        view->hide_tooltips();
        view->hide_dialogs();
      }
    }
    return false;
  });
  
  signal_hide().connect([]{
    while(!Source::View::non_deleted_views.empty()) {
      while(Gtk::Main::events_pending())
        Gtk::Main::iteration(false);
    }
    // TODO 2022 (after Debian Stretch LTS has ended, see issue #354): remove:
    Project::current=nullptr;
  });
  
  Gtk::Settings::get_default()->connect_property_changed("gtk-theme-name", [this] {
    Directories::get().update();
    if(auto notebook=Notebooks::get().get_current_notebook()) {
      if(auto view=notebook->get_current_view())
        notebook->update_status(view);
    }
  });
  
  about.signal_response().connect([this](int d){
    about.hide();
  });
  
  about.set_logo_icon_name("juci");
  about.set_version(Config::get().window.version);
  about.set_authors({"(in order of appearance)",
                     "Ted Johan Kristoffersen", 
                     "Jørgen Lien Sellæg",
                     "Geir Morten Larsen",
                     "Ole Christian Eidheim"});
  about.set_name("About juCi++");
  about.set_program_name("juCi++");
  about.set_comments("This is an open source IDE with high-end features to make your programming experience juicy");
  about.set_license_type(Gtk::License::LICENSE_MIT_X11);
  about.set_transient_for(*this);
} // Window constructor

void Window::configure() {
  Config::get().load();
  auto screen = Gdk::Screen::get_default();
  if(css_provider)
    Gtk::StyleContext::remove_provider_for_screen(screen, css_provider);
  if(Config::get().window.theme_name.empty()) {
    css_provider=Gtk::CssProvider::create();
    Gtk::Settings::get_default()->property_gtk_application_prefer_dark_theme()=(Config::get().window.theme_variant=="dark");
  }
  else
    css_provider=Gtk::CssProvider::get_named(Config::get().window.theme_name, Config::get().window.theme_variant);
  //TODO: add check if theme exists, or else write error to terminal
  Gtk::StyleContext::add_provider_for_screen(screen, css_provider, GTK_STYLE_PROVIDER_PRIORITY_SETTINGS);
  Menu::get().set_keys();
  Terminal::get().configure();
  Directories::get().update();
  if(auto notebook=Notebooks::get().get_current_notebook()) {
    if(auto view=notebook->get_current_view())
      notebook->update_status(view);
  }
}

void Window::set_menu_actions() {
  auto &menu = Menu::get();
  
  menu.add_action("about", [this]() {
    about.show();
    about.present();
  });
  menu.add_action("preferences", [this]() {
    if(auto notebook=Notebooks::get().get_current_notebook()) {
      notebook->open(Config::get().home_juci_path/"config"/"config.json");
    }
  });
  menu.add_action("quit", [this]() {
    close();
  });
  
  menu.add_action("new_file", [this]() {
    if(auto notebook=Notebooks::get().get_current_notebook()) {
      boost::filesystem::path path = Dialog::new_file(notebook->get_current_folder());
      if (!path.empty()) {
        if(boost::filesystem::exists(path)) {
          Terminal::get().print("Error: "+path.string()+" already exists.\n", true);
        }
        else {
          if(filesystem::write(path)) {
            if(Directories::get().path!="")
              Directories::get().update();
            notebook->open(path);
            if(Directories::get().path!="")
              Directories::get().on_save_file(path);
            Terminal::get().print("New file "+path.string()+" created.\n");
          }
          else
            Terminal::get().print("Error: could not create new file "+path.string()+".\n", true);
          }
        }
      }
  });
  menu.add_action("new_folder", [this]() {
    if(auto notebook=Notebooks::get().get_current_notebook()) {
      auto time_now=std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
      boost::filesystem::path path = Dialog::new_folder(notebook->get_current_folder());
      if(!path.empty() && boost::filesystem::exists(path)) {
        boost::system::error_code ec;
        auto last_write_time=boost::filesystem::last_write_time(path, ec);
        if(!ec && last_write_time>=time_now) {
          if(Directories::get().path!="")
            Directories::get().update();
          Terminal::get().print("New folder "+path.string()+" created.\n");
        }
        else
          Terminal::get().print("Error: "+path.string()+" already exists.\n", true);
        Directories::get().select(path);
      }
    }
  });
  menu.add_action("new_project_c", [this]() {
    if(auto notebook=Notebooks::get().get_current_notebook()) {
      boost::filesystem::path project_path = Dialog::new_folder(notebook->get_current_folder());
      if(!project_path.empty()) {
        auto project_name=project_path.filename().string();
        for(size_t c=0;c<project_name.size();c++) {
          if(project_name[c]==' ')
            project_name[c]='_';
        }
        auto cmakelists_path=project_path;
        cmakelists_path/="CMakeLists.txt";
        auto c_main_path=project_path;
        c_main_path/="main.c";
        if(boost::filesystem::exists(cmakelists_path)) {
          Terminal::get().print("Error: "+cmakelists_path.string()+" already exists.\n", true);
          return;
        }
        if(boost::filesystem::exists(c_main_path)) {
          Terminal::get().print("Error: "+c_main_path.string()+" already exists.\n", true);
          return;
        }
        std::string cmakelists="cmake_minimum_required(VERSION 2.8)\n\nproject("+project_name+")\n\nset(CMAKE_C_FLAGS \"${CMAKE_C_FLAGS} -std=c11 -Wall -Wextra\")\n\nadd_executable("+project_name+" main.c)\n";
        std::string c_main="#include <stdio.h>\n\nint main() {\n  printf(\"Hello World!\\n\");\n}\n";
        if(filesystem::write(cmakelists_path, cmakelists) && filesystem::write(c_main_path, c_main)) {
          Directories::get().open(project_path);
          notebook->open(c_main_path);
          Directories::get().update();
          Terminal::get().print("C project "+project_name+" created.\n");
        }
        else
          Terminal::get().print("Error: Could not create project "+project_path.string()+"\n", true);
      }
    }
  });
  menu.add_action("new_project_cpp", [this]() {
    if(auto notebook = Notebooks::get().get_current_notebook()) {
      boost::filesystem::path project_path = Dialog::new_folder(notebook->get_current_folder());
      if(!project_path.empty()) {
        auto project_name=project_path.filename().string();
        for(size_t c=0;c<project_name.size();c++) {
          if(project_name[c]==' ')
            project_name[c]='_';
        }
        auto cmakelists_path=project_path;
        cmakelists_path/="CMakeLists.txt";
        auto cpp_main_path=project_path;
        cpp_main_path/="main.cpp";
        if(boost::filesystem::exists(cmakelists_path)) {
          Terminal::get().print("Error: "+cmakelists_path.string()+" already exists.\n", true);
          return;
        }
        if(boost::filesystem::exists(cpp_main_path)) {
          Terminal::get().print("Error: "+cpp_main_path.string()+" already exists.\n", true);
          return;
        }
        std::string cmakelists="cmake_minimum_required(VERSION 2.8)\n\nproject("+project_name+")\n\nset(CMAKE_CXX_FLAGS \"${CMAKE_CXX_FLAGS} -std=c++1y -Wall -Wextra\")\n\nadd_executable("+project_name+" main.cpp)\n";
        std::string cpp_main="#include <iostream>\n\nint main() {\n  std::cout << \"Hello World!\\n\";\n}\n";
        if(filesystem::write(cmakelists_path, cmakelists) && filesystem::write(cpp_main_path, cpp_main)) {
          Directories::get().open(project_path);
          notebook->open(cpp_main_path);
          Directories::get().update();
          Terminal::get().print("C++ project "+project_name+" created.\n");
        }
        else
          Terminal::get().print("Error: Could not create project "+project_path.string()+"\n", true);
      }
    }
  });
  
  menu.add_action("open_file", [this]() {
    if(auto notebook=Notebooks::get().get_current_notebook()) {
      auto folder_path=notebook->get_current_folder();
      if(auto view=notebook->get_current_view()) {
        if(!Directories::get().path.empty() && !filesystem::file_in_path(view->file_path, Directories::get().path))
          folder_path=view->file_path.parent_path();
      }
      auto path=Dialog::open_file(folder_path);
      if(!path.empty())
        notebook->open(path);
    }
  });
  menu.add_action("open_folder", [this]() {
    if (auto notebook=Notebooks::get().get_current_notebook()) {
      auto path = Dialog::open_folder(notebook->get_current_folder());
      if (!path.empty() && boost::filesystem::exists(path))
        Directories::get().open(path);
    }
  });
  
  menu.add_action("reload_file", [this]() {
    if(auto notebook=Notebooks::get().get_current_notebook()) {
      if(auto view=notebook->get_current_view()) {
        Gtk::MessageDialog dialog(*static_cast<Gtk::Window*>(get_toplevel()), "Reload file!", false, Gtk::MESSAGE_QUESTION, Gtk::BUTTONS_YES_NO);
        dialog.set_default_response(Gtk::RESPONSE_YES);
        dialog.set_secondary_text("Do you want to reload: " + view->file_path.string()+" ? The current buffer will be lost.");
        int result = dialog.run();
        if(result==Gtk::RESPONSE_YES) {
          if(boost::filesystem::exists(view->file_path)) {
            std::ifstream can_read(view->file_path.string());
            if(!can_read) {
              Terminal::get().print("Error: could not read "+view->file_path.string()+"\n", true);
              return;
            }
            can_read.close();
          }
          else {
            Terminal::get().print("Error: "+view->file_path.string()+" does not exist\n", true);
            return;
          }
          
          int line = view->get_buffer()->get_insert()->get_iter().get_line();
          int offset = view->get_buffer()->get_insert()->get_iter().get_line_offset();
          view->load();
          while(Gtk::Main::events_pending())
            Gtk::Main::iteration(false);
          notebook->delete_cursor_locations(view);
          view->place_cursor_at_line_offset(line, offset);
          view->scroll_to_cursor_delayed(view, true, false);
          view->get_buffer()->set_modified(false);
          view->full_reparse();
        }
      }
    }
  });
  
  menu.add_action("save", [this]() {
    if(auto notebook=Notebooks::get().get_current_notebook()) {
      if(auto view=notebook->get_current_view()) {
        if(notebook->save_current()) {
          if(view->file_path==Config::get().home_juci_path/"config"/"config.json") {
            configure();
            for(auto &nbook: Notebooks::get().get_notebooks()) {
              for(size_t c=nbook.size()-1;c<=0;c--) {
                nbook.get_view(c)->configure();
                nbook.configure(c);
              }
            }
          }
        }
      }
    }
  });
  menu.add_action("save_as", [this]() {
    if(auto notebook=Notebooks::get().get_current_notebook()) {
      if(auto view=notebook->get_current_view()) {
        auto path = Dialog::save_file_as(view->file_path);
        if(!path.empty()) {
          std::ofstream file(path, std::ofstream::binary);
          if(file) {
            file << view->get_buffer()->get_text().raw();
            file.close();
            if(Directories::get().path!="")
              Directories::get().update();
            notebook->open(path);
            Terminal::get().print("File saved to: " + notebook->get_current_view()->file_path.string()+"\n");
          }
          else
            Terminal::get().print("Error saving file\n", true);
        }
      }
    }
  });
  
  menu.add_action("print", [this]() {
    if(auto view=Notebooks::get().get_current_view()) {
      auto print_operation=Gtk::PrintOperation::create();
      auto print_compositor=Gsv::PrintCompositor::create(*view);
      
      print_operation->set_job_name(view->file_path.filename().string());
      print_compositor->set_wrap_mode(Gtk::WrapMode::WRAP_WORD_CHAR);
      
      print_operation->signal_begin_print().connect([print_operation, print_compositor](const Glib::RefPtr<Gtk::PrintContext>& print_context) {
        while(!print_compositor->paginate(print_context));
        print_operation->set_n_pages(print_compositor->get_n_pages());
      });
      print_operation->signal_draw_page().connect([print_compositor](const Glib::RefPtr<Gtk::PrintContext>& print_context, int page_nr) {
        print_compositor->draw_page(print_context, page_nr);
      });
      
      print_operation->run(Gtk::PRINT_OPERATION_ACTION_PRINT_DIALOG, *this);
    }
  });
  
  menu.add_action("edit_undo", [this]() {
    if(auto view=Notebooks::get().get_current_view()) {
      auto undo_manager = view->get_source_buffer()->get_undo_manager();
      if (undo_manager->can_undo()) {
        view->disable_spellcheck=true;
        undo_manager->undo();
        view->disable_spellcheck=false;
        view->scroll_to(view->get_buffer()->get_insert());
      }
    }
  });
  menu.add_action("edit_redo", [this]() {
    if(auto view=Notebooks::get().get_current_view()) {
      auto undo_manager = view->get_source_buffer()->get_undo_manager();
      if(undo_manager->can_redo()) {
        view->disable_spellcheck=true;
        undo_manager->redo();
        view->disable_spellcheck=false;
        view->scroll_to(view->get_buffer()->get_insert());
      }
    }
  });
  
  menu.add_action("edit_cut", [this]() {
    auto widget=get_focus();
    if(auto entry=dynamic_cast<Gtk::Entry*>(widget))
      entry->cut_clipboard();
    else if(auto view=Notebooks::get().get_current_view()) {
      view->disable_spellcheck=true;
      view->get_buffer()->cut_clipboard(Gtk::Clipboard::get());
      view->disable_spellcheck=false;
    }
  });
  menu.add_action("edit_copy", [this]() {
    auto widget=get_focus();
    if(auto entry=dynamic_cast<Gtk::Entry*>(widget))
      entry->copy_clipboard();
    else if(auto text_view=dynamic_cast<Gtk::TextView*>(widget))
      text_view->get_buffer()->copy_clipboard(Gtk::Clipboard::get());
  });
  menu.add_action("edit_paste", [this]() {
    auto widget=get_focus();
    if(auto entry=dynamic_cast<Gtk::Entry*>(widget))
      entry->paste_clipboard();
    else if(auto view=Notebooks::get().get_current_view()) {
      view->disable_spellcheck=true;
      view->paste();
      view->disable_spellcheck=false;
    }
  });
  
  menu.add_action("edit_find", [this]() {
    search_and_replace_entry();
  });
  
  menu.add_action("source_spellcheck", [this]() {
    if(auto view=Notebooks::get().get_current_view()) {
      view->remove_spellcheck_errors();
      view->spellcheck();
    }
  });
  menu.add_action("source_spellcheck_clear", [this]() {
    if(auto view=Notebooks::get().get_current_view())
      view->remove_spellcheck_errors();
  });
  menu.add_action("source_spellcheck_next_error", [this]() {
    if(auto view=Notebooks::get().get_current_view())
      view->goto_next_spellcheck_error();
  });
  
  menu.add_action("source_git_next_diff", [this]() {
    if(auto view=Notebooks::get().get_current_view())
      view->git_goto_next_diff();
  });
  menu.add_action("source_git_show_diff", [this]() {
    if (auto notebook=Notebooks::get().get_current_notebook()) {
      if(auto view=notebook->get_current_view()) {
        auto diff_details=view->git_get_diff_details();
        if(diff_details.empty())
          return;
        std::string postfix;
        if(diff_details.size()>2) {
          size_t pos=diff_details.find("@@", 2);
          if(pos!=std::string::npos)
            postfix=diff_details.substr(0, pos+2);
        }
        notebook->open(view->file_path.string()+postfix+".diff");
        if(auto new_view=notebook->get_current_view()) {
          if(new_view->get_buffer()->get_text().empty()) {
            new_view->get_source_buffer()->begin_not_undoable_action();
            new_view->get_buffer()->set_text(diff_details);
            new_view->get_source_buffer()->end_not_undoable_action();
            new_view->get_buffer()->set_modified(false);
          }
        }
      }
    }
  });
  
  menu.add_action("source_indentation_set_buffer_tab", [this]() {
    set_tab_entry();
  });
  menu.add_action("source_indentation_auto_indent_buffer", [this]() {
    auto view=Notebooks::get().get_current_view();
    if(view && view->format_style) {
      view->disable_spellcheck=true;
      view->format_style(true);
      view->disable_spellcheck=false;
    }
  });
  
  menu.add_action("source_goto_line", [this]() {
    goto_line_entry();
  });
  menu.add_action("source_center_cursor", [this]() {
    if(auto view=Notebooks::get().get_current_view())
      view->scroll_to_cursor_delayed(view, true, false);
  });
  menu.add_action("source_cursor_history_back", [this]() {
    if (auto notebook=Notebooks::get().get_current_notebook()) {
      if(notebook->cursor_locations.size()==0)
        return;
      if(notebook->current_cursor_location==static_cast<size_t>(-1))
        notebook->current_cursor_location=notebook->cursor_locations.size()-1;
      
      auto cursor_location=&notebook->cursor_locations.at(notebook->current_cursor_location);
      // Move to current position if current position's view is not current view
      // (in case one is looking at a new file but has not yet placed the cursor within the file)
      if(cursor_location->view!=notebook->get_current_view())
        notebook->open(cursor_location->view->file_path);
      else {
        if(notebook->cursor_locations.size()<=1)
          return;
        if(notebook->current_cursor_location==0)
          return;
        
        --notebook->current_cursor_location;
        cursor_location=&notebook->cursor_locations.at(notebook->current_cursor_location);
        if(notebook->get_current_view()!=cursor_location->view)
          notebook->open(cursor_location->view->file_path);
      }
      notebook->disable_next_update_cursor_locations=true;
      cursor_location->view->get_buffer()->place_cursor(cursor_location->mark->get_iter());
      cursor_location->view->scroll_to_cursor_delayed(cursor_location->view, true, false);
    }
  });
  menu.add_action("source_cursor_history_forward", [this]() {
    if(auto notebook=Notebooks::get().get_current_notebook()) {
      if(notebook->cursor_locations.size()<=1)
        return;
      if(notebook->current_cursor_location==static_cast<size_t>(-1))
        notebook->current_cursor_location=notebook->cursor_locations.size()-1;
      if(notebook->current_cursor_location==notebook->cursor_locations.size()-1)
        return;
      
      ++notebook->current_cursor_location;
      auto &cursor_location=notebook->cursor_locations.at(notebook->current_cursor_location);
      if(notebook->get_current_view()!=cursor_location.view)
        notebook->open(cursor_location.view->file_path);
      notebook->disable_next_update_cursor_locations=true;
      cursor_location.view->get_buffer()->place_cursor(cursor_location.mark->get_iter());
      cursor_location.view->scroll_to_cursor_delayed(cursor_location.view, true, false);
    }
  });
  
  menu.add_action("source_show_completion", [this] {
    if(auto view=Notebooks::get().get_current_view()) {
      if(view->non_interactive_completion)
        view->non_interactive_completion();
      else
        g_signal_emit_by_name(view->gobj(), "show-completion");
    }
  });
  
  menu.add_action("source_find_symbol_ctags", [this]() {
    auto notebook=Notebooks::get().get_current_notebook();
    if(!notebook)
      return;
    
    auto view=notebook->get_current_view();
    
    boost::filesystem::path search_path;
    if(view)
      search_path=view->file_path.parent_path();
    else if(!Directories::get().path.empty())
      search_path=Directories::get().path;
    else {
      boost::system::error_code ec;
      search_path=boost::filesystem::current_path(ec);
      if(ec) {
        Terminal::get().print("Error: could not find current path\n", true);
        return;
      }
    }
    auto pair=Ctags::get_result(search_path);
    
    auto path=std::move(pair.first);
    auto stream=std::move(pair.second);
    stream->seekg(0, std::ios::end);
    if(stream->tellg()==0) {
      Info::get().print("No symbols found in current project");
      return;
    }
    stream->seekg(0, std::ios::beg);
    
    if(view) {
      auto dialog_iter=view->get_iter_for_dialog();
      SelectionDialog::create(view, view->get_buffer()->create_mark(dialog_iter), true, true);
    }
    else
      SelectionDialog::create(true, true);
    
    std::vector<Source::Offset> rows;
      
    std::string line;
    while(std::getline(*stream, line)) {
      auto location=Ctags::get_location(line, true);
      
      std::string row=location.file_path.string()+":"+std::to_string(location.line+1)+": "+location.source;
      rows.emplace_back(Source::Offset(location.line, location.index, location.file_path));
      SelectionDialog::get()->add_row(row);
    }
      
    if(rows.size()==0)
      return;
    SelectionDialog::get()->on_select=[this, notebook, rows=std::move(rows), path=std::move(path)](unsigned int index, const std::string &text, bool hide_window) {
      if(index>=rows.size())
        return;
      auto offset=rows[index];
      auto full_path=path/offset.file_path;
      if(!boost::filesystem::is_regular_file(full_path))
        return;
      notebook->open(full_path);
      auto view=notebook->get_current_view();
      view->place_cursor_at_line_index(offset.line, offset.index);
      view->scroll_to_cursor_delayed(view, true, false);
      view->hide_tooltips();
    };
    if(view)
      view->hide_tooltips();
    SelectionDialog::get()->show();
  });
  
  menu.add_action("source_find_file", [this]() {
    auto view = Notebooks::get().get_current_view();

    boost::filesystem::path search_path;
    if(view)
      search_path=view->file_path.parent_path();
    else if(!Directories::get().path.empty())
      search_path=Directories::get().path;
    else {
      boost::system::error_code ec;
      search_path=boost::filesystem::current_path(ec);
      if(ec) {
        Terminal::get().print("Error: could not find current path\n", true);
        return;
      }
    }
    auto build=Project::Build::create(search_path);
    auto project_path=build->project_path;
    boost::filesystem::path default_path, debug_path;
    if(!project_path.empty()) {
      search_path=project_path;
      default_path = build->get_default_path();
      debug_path = build->get_debug_path();
    }
  
    if(view) {
      auto dialog_iter=view->get_iter_for_dialog();
      SelectionDialog::create(view, view->get_buffer()->create_mark(dialog_iter), true, true);
    }
    else
      SelectionDialog::create(true, true);
    
    std::unordered_set<std::string> buffer_paths;
    // TODO replace Notebook with Notebooks
    for(auto &notebook: Notebooks::get().get_notebooks()) {
      for(auto view: notebook.get_views())
        buffer_paths.emplace(view->file_path.string());
    }
    
    std::vector<boost::filesystem::path> paths;
    // populate with all files in search_path
    for (boost::filesystem::recursive_directory_iterator iter(search_path), end; iter != end; iter++) {
      auto path = iter->path();
      // ignore folders
      if (!boost::filesystem::is_regular_file(path)) {
        if(path==default_path || path==debug_path || path.filename()==".git")
          iter.no_push();
        continue;
      }
        
      // remove project base path
      auto row_str = filesystem::get_relative_path(path, search_path).string();
      if(buffer_paths.count(path.string()))
        row_str="<b>"+row_str+"</b>";
      paths.emplace_back(path);
      SelectionDialog::get()->add_row(row_str);
    }
    
    if(paths.empty()) {
      Info::get().print("No files found in current project");
      return;
    }
  
    SelectionDialog::get()->on_select=[this, paths=std::move(paths)](unsigned int index, const std::string &text, bool hide_window) {
      if(index>=paths.size())
        return;
      if (auto notebook=Notebooks::get().get_current_notebook()) {
        notebook->open(paths[index]);
        if (auto view=notebook->get_current_view())
          view->hide_tooltips();
      }
    };
  
    if(view)
      view->hide_tooltips();
    SelectionDialog::get()->show();
  
  });
  
  menu.add_action("source_comments_toggle", [this]() {
    if(auto view=Notebooks::get().get_current_view()) {
      if(view->toggle_comments) {
        view->toggle_comments();
      }
    }
  });
  menu.add_action("source_comments_add_documentation", [this]() {
    if(auto notebook=Notebooks::get().get_current_notebook()) {
      if(auto view=notebook->get_current_view()) {
        if(view->get_documentation_template) {
          auto documentation_template=view->get_documentation_template();
          auto offset=std::get<0>(documentation_template);
          if(offset) {
            if(!boost::filesystem::is_regular_file(offset.file_path))
              return;
            notebook->open(offset.file_path);
            auto view=notebook->get_current_view();
            auto iter=view->get_buffer()->get_iter_at_line_index(offset.line, offset.index);
            view->get_buffer()->insert(iter, std::get<1>(documentation_template));
            iter=view->get_buffer()->get_iter_at_line_index(offset.line, offset.index);
            iter.forward_chars(std::get<2>(documentation_template));
            view->get_buffer()->place_cursor(iter);
            view->scroll_to_cursor_delayed(view, true, false);
          }
        }
      }
    }
  });
  menu.add_action("source_find_documentation", [this]() {
    if(auto view=Notebooks::get().get_current_view()) {
      if(view->get_token_data) {
        auto data=view->get_token_data();        
        if(data.size()>0) {
          auto documentation_search=Config::get().source.documentation_searches.find(data[0]);
          if(documentation_search!=Config::get().source.documentation_searches.end()) {
            std::string token_query;
            for(size_t c=1;c<data.size();c++) {
              if(data[c].size()>0) {
                if(token_query.size()>0)
                  token_query+=documentation_search->second.separator;
                token_query+=data[c];
              }
            }
            if(token_query.size()>0) {
              std::unordered_map<std::string, std::string>::iterator query;
              if(data[1].size()>0)
                query=documentation_search->second.queries.find(data[1]);
              else
                query=documentation_search->second.queries.find("@empty");
              if(query==documentation_search->second.queries.end())
                query=documentation_search->second.queries.find("@any");
              
              if(query!=documentation_search->second.queries.end()) {
                std::string uri=query->second+token_query;
#ifdef __APPLE__
                Terminal::get().process("open \""+uri+"\"");
#else
                GError* error=nullptr;
#if GTK_VERSION_GE(3, 22)
                gtk_show_uri_on_window(nullptr, uri.c_str(), GDK_CURRENT_TIME, &error);
#else
                gtk_show_uri(nullptr, uri.c_str(), GDK_CURRENT_TIME, &error);
#endif
                g_clear_error(&error);
#endif
              }
            }
          }
        }
      }
    }
  });
  
  menu.add_action("source_goto_declaration", [this]() {
    if(auto notebook=Notebooks::get().get_current_notebook()) {
      if(auto view=notebook->get_current_view()) {
        if(view->get_declaration_location) {
          auto location=view->get_declaration_location();
          if(location) {
            if(!boost::filesystem::is_regular_file(location.file_path))
              return;
            notebook->open(location.file_path);
            auto view=notebook->get_current_view();
            auto line=static_cast<int>(location.line);
            auto index=static_cast<int>(location.index);
            view->place_cursor_at_line_index(line, index);
            view->scroll_to_cursor_delayed(view, true, false);
          }
        }
      }
    }
  });
  auto goto_selected_location=[](Source::View *view, const std::vector<Source::Offset> &locations) {
    if(!locations.empty()) {
      auto dialog_iter=view->get_iter_for_dialog();
      SelectionDialog::create(view, view->get_buffer()->create_mark(dialog_iter), true, true);
      std::vector<Source::Offset> rows;
      auto project_path=Project::Build::create(view->file_path)->project_path;
      if(project_path.empty()) {
        if(!Directories::get().path.empty())
          project_path=Directories::get().path;
        else
          project_path=view->file_path.parent_path();
      }
      for(auto &location: locations) {
        auto path=filesystem::get_relative_path(filesystem::get_normal_path(location.file_path), project_path);
        if(path.empty())
          path=location.file_path.filename();
        auto row=path.string()+":"+std::to_string(location.line+1);
        rows.emplace_back(location);
        SelectionDialog::get()->add_row(row);
      }
      
      if(rows.size()==0)
        return;
      else if(rows.size()==1) {
        auto location=*rows.begin();
        if(!boost::filesystem::is_regular_file(location.file_path))
          return;
        if (auto notebook=Notebooks::get().get_current_notebook()) {
          notebook->open(location.file_path);
          auto view=notebook->get_current_view();
          auto line=static_cast<int>(location.line);
          auto index=static_cast<int>(location.index);
          view->place_cursor_at_line_index(line, index);
          view->scroll_to_cursor_delayed(view, true, false);
        }
        return;
      }
      SelectionDialog::get()->on_select=[rows=std::move(rows)](unsigned int index, const std::string &text, bool hide_window) {
        if(index>=rows.size())
          return;
        auto location=rows[index];
        if(!boost::filesystem::is_regular_file(location.file_path))
          return;
        if (auto notebook=Notebooks::get().get_current_notebook()) {
          notebook->open(location.file_path);
          auto view=notebook->get_current_view();
          view->place_cursor_at_line_index(location.line, location.index);
          view->scroll_to_cursor_delayed(view, true, false);
          view->hide_tooltips();
        }
      };
      view->hide_tooltips();
      SelectionDialog::get()->show();
    }
  };
  menu.add_action("source_goto_implementation", [this, goto_selected_location]() {
    if(auto view=Notebooks::get().get_current_view()) {
      if(view->get_implementation_locations)
        goto_selected_location(view, view->get_implementation_locations());
    }
  });
  menu.add_action("source_goto_declaration_or_implementation", [this, goto_selected_location]() {
    if(auto view=Notebooks::get().get_current_view()) {
      if(view->get_declaration_or_implementation_locations)
        goto_selected_location(view, view->get_declaration_or_implementation_locations());
    }
  });

  menu.add_action("source_goto_usage", [this]() {
    if(auto notebook=Notebooks::get().get_current_notebook()) {
      if(auto view=notebook->get_current_view()) {
        if(view->get_usages) {
          auto usages=view->get_usages();
          if(!usages.empty()) {
            auto dialog_iter=view->get_iter_for_dialog();
            SelectionDialog::create(view, view->get_buffer()->create_mark(dialog_iter), true, true);
            std::vector<Source::Offset> rows;
            
            auto iter=view->get_buffer()->get_insert()->get_iter();
            for(auto &usage: usages) {
              std::string row;
              bool current_page=true;
              //add file name if usage is not in current page
              if(view->file_path!=usage.first.file_path) {
                row=usage.first.file_path.filename().string()+":";
                current_page=false;
              }
              row+=std::to_string(usage.first.line+1)+": "+usage.second;
              rows.emplace_back(usage.first);
              SelectionDialog::get()->add_row(row);
              
              //Set dialog cursor to the last row if the textview cursor is at the same line
              if(current_page &&
                 iter.get_line()==static_cast<int>(usage.first.line) && iter.get_line_index()>=static_cast<int>(usage.first.index)) {
                SelectionDialog::get()->set_cursor_at_last_row();
              }
            }
            
            if(rows.size()==0)
              return;
            SelectionDialog::get()->on_select=[this, notebook, rows=std::move(rows)](unsigned int index, const std::string &text, bool hide_window) {
              if(index>=rows.size())
                return;
              auto offset=rows[index];
              if(!boost::filesystem::is_regular_file(offset.file_path))
                return;
              notebook->open(offset.file_path);
              auto view=notebook->get_current_view();
              view->place_cursor_at_line_index(offset.line, offset.index);
              view->scroll_to_cursor_delayed(view, true, false);
              view->hide_tooltips();
            };
            view->hide_tooltips();
            SelectionDialog::get()->show();
          }
        }
      }
    }
  });
  menu.add_action("source_goto_method", [this]() {
    if(auto view=Notebooks::get().get_current_view()) {
      if(view->get_methods) {
        auto methods=Notebooks::get().get_current_view()->get_methods();
        if(!methods.empty()) {
          auto dialog_iter=view->get_iter_for_dialog();
          SelectionDialog::create(view, view->get_buffer()->create_mark(dialog_iter), true, true);
          std::vector<Source::Offset> rows;
          auto iter=view->get_buffer()->get_insert()->get_iter();
          for(auto &method: methods) {
            rows.emplace_back(method.first);
            SelectionDialog::get()->add_row(method.second);
            if(iter.get_line()>=static_cast<int>(method.first.line))
              SelectionDialog::get()->set_cursor_at_last_row();
          }
          SelectionDialog::get()->on_select=[view, rows=std::move(rows)](unsigned int index, const std::string &text, bool hide_window) {
            if(index>=rows.size())
              return;
            auto offset=rows[index];
            view->get_buffer()->place_cursor(view->get_buffer()->get_iter_at_line_index(offset.line, offset.index));
            view->scroll_to(view->get_buffer()->get_insert(), 0.0, 1.0, 0.5);
            view->hide_tooltips();
          };
          view->hide_tooltips();
          SelectionDialog::get()->show();
        }
      }
    }
  });
  menu.add_action("source_rename", [this]() {
    rename_token_entry();
  });
  menu.add_action("source_implement_method", [this]() {
    const static std::string button_text="Insert Method Implementation";
    
    if(auto view=Notebooks::get().get_current_view()) {
      if(view->get_method) {
        auto iter=view->get_buffer()->get_insert()->get_iter();
        if(!EntryBox::get().buttons.empty() && EntryBox::get().buttons.back().get_label()==button_text &&
           iter.ends_line() && iter.starts_line()) {
          EntryBox::get().buttons.back().activate();
          return;
        }
        auto method=view->get_method();
        if(method.empty())
          return;
        
        EntryBox::get().clear();
        EntryBox::get().labels.emplace_back();
        EntryBox::get().labels.back().set_text(method);
        EntryBox::get().buttons.emplace_back(button_text, [this, method=std::move(method)](){
          if(auto view=Notebooks::get().get_current_view()) {
            view->get_buffer()->insert_at_cursor(method);
            EntryBox::get().clear();
          }
        });
        EntryBox::get().show();
      }
    }
  });
  
  menu.add_action("source_goto_next_diagnostic", [this]() {
    if(auto view=Notebooks::get().get_current_view()) {
      if(view->goto_next_diagnostic) {
        view->goto_next_diagnostic();
      }
    }
  });
  menu.add_action("source_apply_fix_its", [this]() {
    if(auto view=Notebooks::get().get_current_view()) {
      if(view->get_fix_its) {
        auto buffer=view->get_buffer();
        auto fix_its=view->get_fix_its();
        std::vector<std::pair<Glib::RefPtr<Gtk::TextMark>, Glib::RefPtr<Gtk::TextMark> > > fix_it_marks;
        for(auto &fix_it: fix_its) {
          auto start_iter=buffer->get_iter_at_line_index(fix_it.offsets.first.line, fix_it.offsets.first.index);
          auto end_iter=buffer->get_iter_at_line_index(fix_it.offsets.second.line, fix_it.offsets.second.index);
          fix_it_marks.emplace_back(buffer->create_mark(start_iter), buffer->create_mark(end_iter));
        }
        size_t c=0;
        buffer->begin_user_action();
        for(auto &fix_it: fix_its) {
          if(fix_it.type==Source::FixIt::Type::INSERT) {
            buffer->insert(fix_it_marks[c].first->get_iter(), fix_it.source);
          }
          if(fix_it.type==Source::FixIt::Type::REPLACE) {
            buffer->erase(fix_it_marks[c].first->get_iter(), fix_it_marks[c].second->get_iter());
            buffer->insert(fix_it_marks[c].first->get_iter(), fix_it.source);
          }
          if(fix_it.type==Source::FixIt::Type::ERASE) {
            buffer->erase(fix_it_marks[c].first->get_iter(), fix_it_marks[c].second->get_iter());
          }
          c++;
        }
        for(auto &mark_pair: fix_it_marks) {
          buffer->delete_mark(mark_pair.first);
          buffer->delete_mark(mark_pair.second);
        }
        buffer->end_user_action();
      }
    }
  });
  
  menu.add_action("project_set_run_arguments", [this]() {
    auto project=Project::create();
    auto run_arguments=project->get_run_arguments();
    if(run_arguments.second.empty())
      return;
    
    EntryBox::get().clear();
    EntryBox::get().labels.emplace_back();
    auto label_it=EntryBox::get().labels.begin();
    label_it->update=[label_it](int state, const std::string& message){
      label_it->set_text("Synopsis: [environment_variable=value]... executable [argument]...\nSet empty to let juCi++ deduce executable.");
    };
    label_it->update(0, "");
    EntryBox::get().entries.emplace_back(run_arguments.second, [this, run_arguments_first=std::move(run_arguments.first)](const std::string &content){
      Project::run_arguments[run_arguments_first]=content;
      EntryBox::get().hide();
    }, 50);
    auto entry_it=EntryBox::get().entries.begin();
    entry_it->set_placeholder_text("Project: Set Run Arguments");
    EntryBox::get().buttons.emplace_back("Project: set run arguments", [this, entry_it](){
      entry_it->activate();
    });
    EntryBox::get().show();
  });
  menu.add_action("compile_and_run", [this]() {
    if(Project::compiling || Project::debugging) {
      Info::get().print("Compile or debug in progress");
      return;
    }
    
    Project::current=Project::create();
    
    if(Config::get().project.save_on_compile_or_run)
      Project::save_files(Project::current->build->project_path);
    
    Project::current->compile_and_run();
  });
  menu.add_action("compile", [this]() {
    if(Project::compiling || Project::debugging) {
      Info::get().print("Compile or debug in progress");
      return;
    }
            
    Project::current=Project::create();
    
    if(Config::get().project.save_on_compile_or_run)
      Project::save_files(Project::current->build->project_path);
    
    Project::current->compile();
  });
  menu.add_action("project_recreate_build", [this]() {
    if(Project::compiling || Project::debugging) {
      Info::get().print("Compile or debug in progress");
      return;
    }
    
    Project::current=Project::create();

    Project::current->recreate_build();
  });
  
  menu.add_action("run_command", [this]() {
    EntryBox::get().clear();
    EntryBox::get().labels.emplace_back();
    auto label_it=EntryBox::get().labels.begin();
    label_it->update=[label_it](int state, const std::string& message){
      label_it->set_text("Run Command directory order: opened directory, file path, current directory");
    };
    label_it->update(0, "");
    EntryBox::get().entries.emplace_back(last_run_command, [this](const std::string& content){
      if(content!="") {
        last_run_command=content;
        auto run_path=Notebooks::get().get_current_folder();
        Terminal::get().async_print("Running: "+content+'\n');
  
        Terminal::get().async_process(content, run_path, [this, content](int exit_status){
          Terminal::get().async_print(content+" returned: "+std::to_string(exit_status)+'\n');
        });
      }
      EntryBox::get().hide();
    }, 30);
    auto entry_it=EntryBox::get().entries.begin();
    entry_it->set_placeholder_text("Command");
    EntryBox::get().buttons.emplace_back("Run command", [this, entry_it](){
      entry_it->activate();
    });
    EntryBox::get().show();
  });
  
  menu.add_action("kill_last_running", [this]() {
    Terminal::get().kill_last_async_process();
  });
  menu.add_action("force_kill_last_running", [this]() {
    Terminal::get().kill_last_async_process(true);
  });
  
#ifdef JUCI_ENABLE_DEBUG
  menu.add_action("debug_set_run_arguments", [this]() {
    auto project=Project::create();
    auto run_arguments=project->debug_get_run_arguments();
    if(run_arguments.second.empty())
      return;
    
    EntryBox::get().clear();
    EntryBox::get().labels.emplace_back();
    auto label_it=EntryBox::get().labels.begin();
    label_it->update=[label_it](int state, const std::string& message){
      label_it->set_text("Synopsis: [environment_variable=value]... executable [argument]...\nSet empty to let juCi++ deduce executable.");
    };
    label_it->update(0, "");
    EntryBox::get().entries.emplace_back(run_arguments.second, [this, run_arguments_first=std::move(run_arguments.first)](const std::string& content){
      Project::debug_run_arguments[run_arguments_first]=content;
      EntryBox::get().hide();
    }, 50);
    auto entry_it=EntryBox::get().entries.begin();
    entry_it->set_placeholder_text("Debug: Set Run Arguments");
    
    if(auto options=project->debug_get_options()) {
      EntryBox::get().buttons.emplace_back("", [this, options]() {
        options->set_visible(true);
      });
      EntryBox::get().buttons.back().set_image_from_icon_name("preferences-system");
      EntryBox::get().buttons.back().set_always_show_image(true);
      EntryBox::get().buttons.back().set_tooltip_text("Additional Options");
      options->set_relative_to(EntryBox::get().buttons.back());
    }
    
    EntryBox::get().buttons.emplace_back("Debug: set run arguments", [this, entry_it](){
      entry_it->activate();
    });
    EntryBox::get().show();
  });
  menu.add_action("debug_start_continue", [this](){
    if(Project::compiling) {
      Info::get().print("Compile in progress");
      return;
    }
    else if(Project::debugging) {
      Project::current->debug_continue();
      return;
    }
        
    Project::current=Project::create();
    
    if(Config::get().project.save_on_compile_or_run)
      Project::save_files(Project::current->build->project_path);
    
    Project::current->debug_start();
  });
  menu.add_action("debug_stop", [this]() {
    if(Project::current)
      Project::current->debug_stop();
  });
  menu.add_action("debug_kill", [this]() {
    if(Project::current)
      Project::current->debug_kill();
  });
  menu.add_action("debug_step_over", [this]() {
    if(Project::current)
      Project::current->debug_step_over();
  });
  menu.add_action("debug_step_into", [this]() {
    if(Project::current)
      Project::current->debug_step_into();
  });
  menu.add_action("debug_step_out", [this]() {
    if(Project::current)
      Project::current->debug_step_out();
  });
  menu.add_action("debug_backtrace", [this]() {
    if(Project::current)
      Project::current->debug_backtrace();
  });
  menu.add_action("debug_show_variables", [this]() {
    if(Project::current)
      Project::current->debug_show_variables();
  });
  menu.add_action("debug_run_command", [this]() {
    EntryBox::get().clear();
    EntryBox::get().entries.emplace_back(last_run_debug_command, [this](const std::string& content){
      if(content!="") {
        if(Project::current)
          Project::current->debug_run_command(content);
        last_run_debug_command=content;
      }
      EntryBox::get().hide();
    }, 30);
    auto entry_it=EntryBox::get().entries.begin();
    entry_it->set_placeholder_text("Debug Command");
    EntryBox::get().buttons.emplace_back("Run debug command", [this, entry_it](){
      entry_it->activate();
    });
    EntryBox::get().show();
  });
  menu.add_action("debug_toggle_breakpoint", [this](){
    if(auto view=Notebooks::get().get_current_view()) {
      if(view->toggle_breakpoint)
        view->toggle_breakpoint(view->get_buffer()->get_insert()->get_iter().get_line());
    }
  });
  menu.add_action("debug_goto_stop", [this](){
    if(Project::debugging) {
      if(!Project::debug_stop.first.empty()) {
        if(auto notebook=Notebooks::get().get_current_notebook()) {
          notebook->open(Project::debug_stop.first);
          if(auto view=notebook->get_current_view()) {
            int line=Project::debug_stop.second.first;
            int index=Project::debug_stop.second.second;
            view->place_cursor_at_line_index(line, index);
            view->scroll_to_cursor_delayed(view, true, true);
          }
        }
      }
    }
  });
  
  Project::debug_update_status("");
#endif
  
  menu.add_action("next_tab", [this]() {
    Notebooks::get().next();
  });
  menu.add_action("previous_tab", [this]() {
    Notebooks::get().previous();
  });
  menu.add_action("close_tab", [this]() {
    if(Notebooks::get().get_current_view())
      Notebooks::get().close_current();
  });
  menu.add_action("window_toggle_split", [this] {
    Notebooks::get().toggle_split();
  });
  menu.add_action("window_add_notebook", [this] {
    notebookWindows.emplace_back();
  });
  menu.add_action("window_toggle_full_screen", [this] {
    if(this->get_window()->get_state() & Gdk::WindowState::WINDOW_STATE_FULLSCREEN)
      unfullscreen();
    else
      fullscreen();
  });
  menu.add_action("window_toggle_tabs", [this] {
    Notebooks::get().toggle_tabs();
  });
  menu.add_action("window_clear_terminal", [this] {
    Terminal::get().clear();
  });
}

void Window::activate_menu_items() {
  auto &menu = Menu::get();
  auto view=Notebooks::get().get_current_view();
  
  menu.actions["reload_file"]->set_enabled(view);
  menu.actions["source_spellcheck"]->set_enabled(view);
  menu.actions["source_spellcheck_clear"]->set_enabled(view);
  menu.actions["source_spellcheck_next_error"]->set_enabled(view);
  menu.actions["source_git_next_diff"]->set_enabled(view);
  menu.actions["source_git_show_diff"]->set_enabled(view);
  menu.actions["source_indentation_set_buffer_tab"]->set_enabled(view);
  menu.actions["source_goto_line"]->set_enabled(view);
  menu.actions["source_center_cursor"]->set_enabled(view);
  
  menu.actions["source_indentation_auto_indent_buffer"]->set_enabled(view && view->format_style);
  menu.actions["source_comments_toggle"]->set_enabled(view && view->toggle_comments);
  menu.actions["source_comments_add_documentation"]->set_enabled(view && view->get_documentation_template);
  menu.actions["source_find_documentation"]->set_enabled(view && view->get_token_data);
  menu.actions["source_goto_declaration"]->set_enabled(view && view->get_declaration_location);
  menu.actions["source_goto_implementation"]->set_enabled(view && view->get_implementation_locations);
  menu.actions["source_goto_declaration_or_implementation"]->set_enabled(view && view->get_declaration_or_implementation_locations);
  menu.actions["source_goto_usage"]->set_enabled(view && view->get_usages);
  menu.actions["source_goto_method"]->set_enabled(view && view->get_methods);
  menu.actions["source_rename"]->set_enabled(view && view->rename_similar_tokens);
  menu.actions["source_implement_method"]->set_enabled(view && view->get_method);
  menu.actions["source_goto_next_diagnostic"]->set_enabled(view && view->goto_next_diagnostic);
  menu.actions["source_apply_fix_its"]->set_enabled(view && view->get_fix_its);
#ifdef JUCI_ENABLE_DEBUG
  Project::debug_activate_menu_items();
#endif
}

bool Window::on_key_press_event(GdkEventKey *event) {
  if(event->keyval==GDK_KEY_Escape) {
    EntryBox::get().hide();
  }
#ifdef __APPLE__ //For Apple's Command-left, right, up, down keys
  else if((event->state & GDK_META_MASK)>0 && (event->state & GDK_MOD1_MASK)==0) {
    if(event->keyval==GDK_KEY_Left || event->keyval==GDK_KEY_KP_Left) {
      event->keyval=GDK_KEY_Home;
      event->state=event->state & GDK_SHIFT_MASK;
      event->hardware_keycode=115;
    }
    else if(event->keyval==GDK_KEY_Right || event->keyval==GDK_KEY_KP_Right) {
      event->keyval=GDK_KEY_End;
      event->state=event->state & GDK_SHIFT_MASK;
      event->hardware_keycode=119;
    }
    else if(event->keyval==GDK_KEY_Up || event->keyval==GDK_KEY_KP_Up) {
      event->keyval=GDK_KEY_Home;
      event->state=event->state & GDK_SHIFT_MASK;
      event->state+=GDK_CONTROL_MASK;
      event->hardware_keycode=115;
    }
    else if(event->keyval==GDK_KEY_Down || event->keyval==GDK_KEY_KP_Down) {
      event->keyval=GDK_KEY_End;
      event->state=event->state & GDK_SHIFT_MASK;
      event->state+=GDK_CONTROL_MASK;
      event->hardware_keycode=119;
    }
  }
#endif

  if(SelectionDialog::get() && SelectionDialog::get()->is_visible()) {
    if(SelectionDialog::get()->on_key_press(event))
      return true;
  }

  return Gtk::ApplicationWindow::on_key_press_event(event);
}

bool Window::on_delete_event(GdkEventAny *event) {
  Notebooks::get().save_session();
  for(auto &notebook: Notebooks::get().get_notebooks()) {
    for(size_t c=notebook.size()-1;c!=static_cast<size_t>(-1);--c) {
      if(!notebook.close(c))
        return true;
    }
  }
  Terminal::get().kill_async_processes();
#ifdef JUCI_ENABLE_DEBUG
  if(Project::current)
    Project::current->debug_cancel();
#endif

  return false;
}

void Window::search_and_replace_entry() {
  EntryBox::get().clear();
  EntryBox::get().labels.emplace_back();
  auto label_it=EntryBox::get().labels.begin();
  label_it->update=[label_it](int state, const std::string& message){
    if(state==0) {
      try {
        auto number = stoi(message);
        if(number==0)
          label_it->set_text("");
        else if(number==1)
          label_it->set_text("1 result found");
        else if(number>1)
          label_it->set_text(message+" results found");
      }
      catch(const std::exception &e) {}
    }
  };
  EntryBox::get().entries.emplace_back(last_search, [this](const std::string& content){
    if(auto view=Notebooks::get().get_current_view())
      view->search_forward();
  });
  auto search_entry_it=EntryBox::get().entries.begin();
  search_entry_it->set_placeholder_text("Find");
  if(auto view=Notebooks::get().get_current_view()) {
    view->update_search_occurrences=[label_it](int number){
      label_it->update(0, std::to_string(number));
    };
    view->search_highlight(search_entry_it->get_text(), case_sensitive_search, regex_search);
  }
  search_entry_it->signal_key_press_event().connect([this](GdkEventKey* event){
    if((event->keyval==GDK_KEY_Return || event->keyval==GDK_KEY_KP_Enter) && (event->state&GDK_SHIFT_MASK)>0) {
      if(auto view=Notebooks::get().get_current_view())
        view->search_backward();
    }
    return false;
  });
  search_entry_it->signal_changed().connect([this, search_entry_it](){
    last_search=search_entry_it->get_text();
    if(auto view=Notebooks::get().get_current_view())
      view->search_highlight(search_entry_it->get_text(), case_sensitive_search, regex_search);
  });

  EntryBox::get().entries.emplace_back(last_replace, [this](const std::string &content){
    if(auto view=Notebooks::get().get_current_view())
      view->replace_forward(content);
  });
  auto replace_entry_it=EntryBox::get().entries.begin();
  replace_entry_it++;
  replace_entry_it->set_placeholder_text("Replace");
  replace_entry_it->signal_key_press_event().connect([this, replace_entry_it](GdkEventKey* event){
    if((event->keyval==GDK_KEY_Return || event->keyval==GDK_KEY_KP_Enter) && (event->state&GDK_SHIFT_MASK)>0) {
      if(auto view=Notebooks::get().get_current_view())
        view->replace_backward(replace_entry_it->get_text());
    }
    return false;
  });
  replace_entry_it->signal_changed().connect([this, replace_entry_it](){
    last_replace=replace_entry_it->get_text();
  });
  
  EntryBox::get().buttons.emplace_back("↑", [this](){
    if(auto view=Notebooks::get().get_current_view())
        view->search_backward();
  });
  EntryBox::get().buttons.back().set_tooltip_text("Find Previous\n\nShortcut: Shift+Enter in the Find entry field");
  EntryBox::get().buttons.emplace_back("⇄", [this, replace_entry_it](){
    if(auto view=Notebooks::get().get_current_view()) {
      view->replace_forward(replace_entry_it->get_text());
    }
  });
  EntryBox::get().buttons.back().set_tooltip_text("Replace Next\n\nShortcut: Enter in the Replace entry field");
  EntryBox::get().buttons.emplace_back("↓", [this](){
    if(auto view=Notebooks::get().get_current_view())
      view->search_forward();
  });
  EntryBox::get().buttons.back().set_tooltip_text("Find Next\n\nShortcut: Enter in the Find entry field");
  EntryBox::get().buttons.emplace_back("Replace All", [this, replace_entry_it](){
    if(auto view=Notebooks::get().get_current_view())
      view->replace_all(replace_entry_it->get_text());
  });
  EntryBox::get().buttons.back().set_tooltip_text("Replace All");
  
  EntryBox::get().toggle_buttons.emplace_back("Aa");
  EntryBox::get().toggle_buttons.back().set_tooltip_text("Match Case");
  EntryBox::get().toggle_buttons.back().set_active(case_sensitive_search);
  EntryBox::get().toggle_buttons.back().on_activate=[this, search_entry_it](){
    case_sensitive_search=!case_sensitive_search;
    if(auto view=Notebooks::get().get_current_view())
      view->search_highlight(search_entry_it->get_text(), case_sensitive_search, regex_search);
  };
  EntryBox::get().toggle_buttons.emplace_back(".*");
  EntryBox::get().toggle_buttons.back().set_tooltip_text("Use Regex");
  EntryBox::get().toggle_buttons.back().set_active(regex_search);
  EntryBox::get().toggle_buttons.back().on_activate=[this, search_entry_it](){
    regex_search=!regex_search;
    if(auto view=Notebooks::get().get_current_view())
      view->search_highlight(search_entry_it->get_text(), case_sensitive_search, regex_search);
  };
  EntryBox::get().signal_hide().connect([this]() {
    for(auto &notebook: Notebooks::get().get_notebooks()) {
      for(auto view: notebook.get_views()) {
        view->update_search_occurrences=nullptr;
        view->search_highlight("", case_sensitive_search, regex_search);
      }
    }
    search_entry_shown=false;
  });
  search_entry_shown=true;
  EntryBox::get().show();
}

void Window::set_tab_entry() {
  EntryBox::get().clear();
  if(auto view=Notebooks::get().get_current_view()) {
    auto tab_char_and_size=view->get_tab_char_and_size();
    
    EntryBox::get().labels.emplace_back();
    auto label_it=EntryBox::get().labels.begin();
    
    EntryBox::get().entries.emplace_back(std::to_string(tab_char_and_size.second));
    auto entry_tab_size_it=EntryBox::get().entries.begin();
    entry_tab_size_it->set_placeholder_text("Tab size");
    
    char tab_char=tab_char_and_size.first;
    std::string tab_char_string;
    if(tab_char==' ')
      tab_char_string="space";
    else if(tab_char=='\t')
      tab_char_string="tab";
      
    EntryBox::get().entries.emplace_back(tab_char_string);
    auto entry_tab_char_it=EntryBox::get().entries.rbegin();
    entry_tab_char_it->set_placeholder_text("Tab char");
    
    const auto activate_function=[this, entry_tab_char_it, entry_tab_size_it, label_it](const std::string& content){
      if(auto view=Notebooks::get().get_current_view()) {
        char tab_char=0;
        unsigned tab_size=0;
        try {
          tab_size = static_cast<unsigned>(std::stoul(entry_tab_size_it->get_text()));
          std::string tab_char_string=entry_tab_char_it->get_text();
          std::transform(tab_char_string.begin(), tab_char_string.end(), tab_char_string.begin(), ::tolower);
          if(tab_char_string=="space")
            tab_char=' ';
          else if(tab_char_string=="tab")
            tab_char='\t';
        }
        catch(const std::exception &e) {}

        if(tab_char!=0 && tab_size>0) {
          view->set_tab_char_and_size(tab_char, tab_size);
          EntryBox::get().hide();
        }
        else {
          label_it->set_text("Tab size must be >0 and tab char set to either 'space' or 'tab'");
        }
      }
    };
    
    entry_tab_char_it->on_activate=activate_function;
    entry_tab_size_it->on_activate=activate_function;
    
    EntryBox::get().buttons.emplace_back("Set tab in current buffer", [this, entry_tab_char_it](){
      entry_tab_char_it->activate();
    });
    
    EntryBox::get().show();
  }
}

void Window::goto_line_entry() {
  EntryBox::get().clear();
  if(Notebooks::get().get_current_view()) {
    EntryBox::get().entries.emplace_back("", [this](const std::string& content){
      if(auto view=Notebooks::get().get_current_view()) {
        try {
          view->place_cursor_at_line_index(stoi(content)-1, 0);
          view->scroll_to_cursor_delayed(view, true, false);
        }
        catch(const std::exception &e) {}  
        EntryBox::get().hide();
      }
    });
    auto entry_it=EntryBox::get().entries.begin();
    entry_it->set_placeholder_text("Line number");
    EntryBox::get().buttons.emplace_back("Go to line", [this, entry_it](){
      entry_it->activate();
    });
    EntryBox::get().show();
  }
}

void Window::rename_token_entry() {
  EntryBox::get().clear();
  if(auto view=Notebooks::get().get_current_view()) {
    if(view->get_token_spelling && view->rename_similar_tokens) {
      auto spelling=std::make_shared<std::string>(view->get_token_spelling());
      if(!spelling->empty()) {
        EntryBox::get().entries.emplace_back(*spelling, [this, view, spelling, iter=view->get_buffer()->get_insert()->get_iter()](const std::string& content){
          //TODO: gtk needs a way to check if iter is valid without dumping g_error message
          //iter->get_buffer() will print such a message, but no segfault will occur
          if(Notebooks::get().get_current_view()==view && content!=*spelling && iter.get_buffer() && view->get_buffer()->get_insert()->get_iter()==iter)
            view->rename_similar_tokens(content);
          else
            Info::get().print("Operation canceled");
          EntryBox::get().hide();
        });
        auto entry_it=EntryBox::get().entries.begin();
        entry_it->set_placeholder_text("New name");
        EntryBox::get().buttons.emplace_back("Rename", [this, entry_it](){
          entry_it->activate();
        });
        EntryBox::get().show();
      }
    }
  }
}
