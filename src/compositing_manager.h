#ifndef _DMR_COMPOSITING_MANAGER
#define _DMR_COMPOSITING_MANAGER 

#include <QtCore>

namespace dmr {
    class CompositingManager: public QObject {
        public:
            static CompositingManager& get();
            virtual ~CompositingManager();
            bool composited() const { return _composited; }

        signals:
            void compositingChanged(bool);

        private:
            CompositingManager();

            bool _composited {false};
    };
}

#endif /* ifndef _DMR_COMPOSITING_MANAGER */

