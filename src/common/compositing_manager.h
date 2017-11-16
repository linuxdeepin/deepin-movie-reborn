#ifndef _DMR_COMPOSITING_MANAGER
#define _DMR_COMPOSITING_MANAGER 

#include <QtCore>

namespace dmr {
enum Platform {
    Unknown,
    X86,  // intel & amd
    Mips, // loongson
    Alpha // sunway
};

using PlayerOption = QPair<QString, QString>;
using PlayerOptionList = QList<PlayerOption>;

class CompositingManager: public QObject {
    public:
        static CompositingManager& get();
        virtual ~CompositingManager();

        static void detectOpenGLEarly();

        // this actually means opengl rendering is capable
        bool composited() const { return _composited; }
        Platform platform() const { return _platform; }

        PlayerOptionList getProfile(const QString& name);
        PlayerOptionList getBestProfile(); // best for current platform and env

    signals:
        void compositingChanged(bool);

    private:
        CompositingManager();
        bool isDriverLoadedCorrectly();
        bool isDirectRendered();

        bool _composited {false};
        Platform _platform {Platform::Unknown};
};
}

#endif /* ifndef _DMR_COMPOSITING_MANAGER */

