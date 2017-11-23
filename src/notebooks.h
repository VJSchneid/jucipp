#ifndef JUCI_NOTEBOOKS_H
#define JUCI_NOTEBOOKS_H

#include "notebook_paned.h"

class Notebooks {
  //Notebooks();
public:
  static Notebooks &get() {
    static Notebooks singleton;
    return singleton;
  }
  
  NotebookPaned &create();
  std::vector<NotebookPaned> &get_notebooks();
  NotebookPaned *get_current_notebook();
  NotebookPaned *get_notebook(size_t index);
  Source::View  *get_current_view();
  
  std::function<void(NotebookPaned*, Source::View*)> on_change_page;
  std::function<void(NotebookPaned*, Source::View*)> on_close_page;
  
  bool save_current();
  void save_session();
  bool close_current();
  void next();
  void previous();
  void toggle_split();
  /// Hide/Show tabs.
  void toggle_tabs();
  boost::filesystem::path get_current_folder();
  
private:
  std::vector<NotebookPaned> notebooksPaned;
  NotebookPaned *currentNotebookPaned=nullptr;
  
};

#endif //JUCI_NOTEBOOKS_H