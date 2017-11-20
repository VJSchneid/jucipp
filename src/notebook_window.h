#ifndef JUCI_NOTEBOOK_WINDOW_H
#define JUCI_NOTEBOOK_WINDOW_H

#include <gtkmm.h>
#include "notebook.h"

class NotebookWindow: public Gtk::Window {
public:
    NotebookWindow();
    
private:
    Notebook notebook;
    
};

#endif // JUCI_NOTEBOOK_WINDOW_H