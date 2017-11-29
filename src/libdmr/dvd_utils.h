#ifndef _DMR_DVD_UTILS_H
#define _DMR_DVD_UTILS_H 

#include <QtCore>

namespace dmr {
namespace dvd {
    // device could be a dev node or a iso file
    QString RetrieveDVDTitle(const QString& device);
}
}

#endif /* ifndef _DMR_DVD_UTILS_H */

