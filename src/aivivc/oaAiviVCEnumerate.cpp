#include "oaAiviVC.h"

#include <functional>
#include <set>

namespace oaAiviVC {

void AiviVC::collectVersionableObjects(
    std::vector<oaCommon::SPtr<oaPlugIn::IDMObject> > &objects,
    oaPlugIn::IDMObject *top,
    oa::oaUInt4 depth,
    bool controlledOnly,
    bool localOnly) const
{
    std::set<std::string> seen;

    std::function<void(oaPlugIn::IDMObject *)> append =
        [&](oaPlugIn::IDMObject *obj) {
            if (!obj) {
                return;
            }

            oa::oaUInt4 status = 0;
            if (controlledOnly || localOnly) {
                status = statusForObject(obj);
                if (controlledOnly && !(status & oaPlugIn::IDMObjectStatus::cControlled)) {
                    return;
                }
                if (localOnly && (status & oaPlugIn::IDMObjectStatus::cRemote)) {
                    return;
                }
            }

            const char *kind = obj->isDMFile() ? "file" :
                               obj->isCellView() ? "cellView" :
                               obj->isCell() ? "cell" :
                               obj->isView() ? "view" :
                               obj->isLib() ? "lib" : "object";
            std::string key = std::string(kind) + ":" + objectRelPath(obj);
            if (seen.insert(key).second) {
                objects.push_back(oaCommon::SPtr<oaPlugIn::IDMObject>(obj));
            }
        };

    std::function<void(oaPlugIn::IDMFile *)> appendFileTree =
        [&](oaPlugIn::IDMFile *file) {
            if (!file) {
                return;
            }
            append(file);

            oaPlugIn::IDMFileIter *followers = NULL;
            file->getFollowers(followers);
            if (!followers) {
                return;
            }
            followers->reset();
            oaPlugIn::IDMFile *follower = NULL;
            while (followers->next(follower)) {
                appendFileTree(follower);
                follower->release();
            }
            followers->release();
        };

    std::function<void(oaPlugIn::IDMContainer *)> collectFiles =
        [&](oaPlugIn::IDMContainer *container) {
            if (!container) {
                return;
            }

            oaPlugIn::IDMFileIter *files = NULL;
            container->getDMFiles(files);
            if (!files) {
                return;
            }

            files->reset();
            oaPlugIn::IDMFile *file = NULL;
            while (files->next(file)) {
                appendFileTree(file);
                file->release();
            }
            files->release();
        };

    std::function<void(oaPlugIn::ICellViewIter *, bool)> collectCellViews =
        [&](oaPlugIn::ICellViewIter *iter, bool collectFilesOnly) {
            if (!iter) {
                return;
            }

            iter->reset();
            oaPlugIn::ICellView *cv = NULL;
            while (iter->next(cv)) {
                if (collectFilesOnly) {
                    collectFiles(cv);
                } else {
                    append(cv);
                }
                cv->release();
            }
            iter->release();
        };

    if (!top) {
        return;
    }

    if (top->isDMFile()) {
        append(top);
        return;
    }

    oaPlugIn::IDMContainer *container =
        top->isContainer() ? static_cast<oaPlugIn::IDMContainer *>(top) : NULL;

    if (depth == oaPlugIn::oacFileVCQueryDepth) {
        collectFiles(container);
        return;
    }

    oaPlugIn::IDMLib *lib = top->isLib() ? static_cast<oaPlugIn::IDMLib *>(top) : NULL;
    oaPlugIn::ICell *cell = top->isCell() ? static_cast<oaPlugIn::ICell *>(top) : NULL;
    oaPlugIn::IView *view = top->isView() ? static_cast<oaPlugIn::IView *>(top) : NULL;
    oaPlugIn::ICellView *cellView =
        top->isCellView() ? static_cast<oaPlugIn::ICellView *>(top) : NULL;

    if (depth == oaPlugIn::oacCellVCQueryDepth) {
        if (cell) {
            append(cell);
        } else if (lib) {
            oaPlugIn::ICellIter *cells = NULL;
            lib->getCells(cells);
            if (cells) {
                cells->reset();
                oaPlugIn::ICell *item = NULL;
                while (cells->next(item)) {
                    append(item);
                    item->release();
                }
                cells->release();
            }
        }
    } else if (depth == oaPlugIn::oacViewVCQueryDepth) {
        if (view) {
            append(view);
        } else if (lib) {
            oaPlugIn::IViewIter *views = NULL;
            lib->getViews(views);
            if (views) {
                views->reset();
                oaPlugIn::IView *item = NULL;
                while (views->next(item)) {
                    append(item);
                    item->release();
                }
                views->release();
            }
        }
    } else if (depth == oaPlugIn::oacCellViewVCQueryDepth) {
        if (cellView) {
            append(cellView);
        } else if (cell) {
            oaPlugIn::ICellViewIter *cvs = NULL;
            cell->getCellViews(cvs);
            collectCellViews(cvs, false);
        } else if (view) {
            oaPlugIn::ICellViewIter *cvs = NULL;
            view->getCellViews(cvs);
            collectCellViews(cvs, false);
        } else if (lib) {
            oaPlugIn::ICellViewIter *cvs = NULL;
            lib->getCellViews(cvs);
            collectCellViews(cvs, false);
        }
    } else {
        collectFiles(container);
        if (cellView) {
            collectFiles(cellView);
        } else if (cell) {
            oaPlugIn::ICellViewIter *cvs = NULL;
            cell->getCellViews(cvs);
            collectCellViews(cvs, true);
        } else if (view) {
            oaPlugIn::ICellViewIter *cvs = NULL;
            view->getCellViews(cvs);
            collectCellViews(cvs, true);
        } else if (lib) {
            oaPlugIn::ICellViewIter *cvs = NULL;
            lib->getCellViews(cvs);
            collectCellViews(cvs, true);
        }
    }

}

} // namespace oaAiviVC
