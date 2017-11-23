#include "notebooks.h"

NotebookPaned &Notebooks::create() {
    notebooksPaned.emplace_back();
    currentNotebookPaned = &notebooksPaned.back();
    return notebooksPaned.back();
}
 
std::vector<NotebookPaned> &Notebooks::get_notebooks() {
    return notebooksPaned;
}

NotebookPaned *Notebooks::get_current_notebook() {
    return currentNotebookPaned;
}

NotebookPaned *Notebooks::get_notebook(size_t index) {
    if(index>=notebooksPaned.size())
        return nullptr;
    return notebooksPaned.data() + index;
}

Source::View *Notebooks::get_current_view() {
    if(currentNotebookPaned)
        return currentNotebookPaned->get_current_view();
    return nullptr;
}

bool Notebooks::save_current() {
    if(currentNotebookPaned)
        return currentNotebookPaned->save_current();
    return false;
}

void Notebooks::save_session() {
    for(auto &notebook: notebooksPaned) {
        notebook.save_session();
    }
}

bool Notebooks::close_current() {
    if(currentNotebookPaned)
        return currentNotebookPaned->save_current();
    return false;
}

void Notebooks::next() {
    if(currentNotebookPaned)
        currentNotebookPaned->next();
}

void Notebooks::previous() {
    if(currentNotebookPaned)
        currentNotebookPaned->previous();
}

void Notebooks::toggle_split() {
    if(currentNotebookPaned)
        currentNotebookPaned->toggle_split();
}

void Notebooks::toggle_tabs() {
    if(currentNotebookPaned)
        currentNotebookPaned->toggle_tabs();
}

boost::filesystem::path Notebooks::get_current_folder() {
    if(currentNotebookPaned)
        return currentNotebookPaned->get_current_folder();
    return boost::filesystem::path();
}