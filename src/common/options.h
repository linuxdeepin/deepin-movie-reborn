#ifndef _DMR_OPTIONS_H
#define _DMR_OPTIONS_H

#include <QtCore>

namespace dmr {
class CommandLineManager: public QCommandLineParser {
public:
    static CommandLineManager& get();
    bool verbose() const;
    bool debug() const;
    QString openglMode() const;
    QString overrideConfig() const;
    
    // for vpu level debug
    int debugFrameCount() const; 
    bool useGAL() const;
    bool vpuDemoMode() const;

    QString dvdDevice() const;

private:
    CommandLineManager();
};

}

#endif /* ifndef _DMR_OPTIONS_H */
