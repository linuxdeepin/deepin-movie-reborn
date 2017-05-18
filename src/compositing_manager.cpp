#include "compositing_manager.h"


namespace dmr {
    static CompositingManager* _compManager = nullptr;

    CompositingManager& CompositingManager::get() {
        if(!_compManager) {
            _compManager = new CompositingManager();
        }

        return *_compManager;
    }

    //void compositingChanged(bool);

    CompositingManager::CompositingManager() {
        _composited = true;
    }

    CompositingManager::~CompositingManager() {
    }
}

