/* 
 * (c) 2017, Deepin Technology Co., Ltd. <support@deepin.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * is provided AS IS, WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, and
 * NON-INFRINGEMENT.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * In addition, as a special exception, the copyright holders give
 * permission to link the code of portions of this program with the
 * OpenSSL library under certain conditions as described in each
 * individual source file, and distribute linked combinations
 * including the two.
 * You must obey the GNU General Public License in all respects
 * for all of the code used other than OpenSSL.  If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so.  If you
 * do not wish to do so, delete this exception statement from your
 * version.  If you delete this exception statement from all source
 * files in the program, then also delete it here.
 */
#include "dvd_utils.h"

#include <dvdnav/dvdnav.h>

namespace dmr {
namespace dvd {

QString RetrieveDVDTitle(const QString& device)
{
    qDebug() << "device" << device;
    const char *title = NULL;

    dvdnav_t *handle = NULL;
    auto res = dvdnav_open(&handle, device.toUtf8().constData());
    if (res == DVDNAV_STATUS_ERR) {
        qWarning() << "dvdnav open " << device << "failed";
        return "";
    }

    int32_t nr_titles = 0;
    res = dvdnav_get_number_of_titles(handle, &nr_titles);
    if (res == DVDNAV_STATUS_ERR) {
        goto on_error;
    }

    res = dvdnav_get_title_string(handle, &title);
    if (res == DVDNAV_STATUS_ERR) {
        goto on_error;
    }

#if 0
    uint64_t max_duration = -1;
    QString title = "";
    //uint32_t dvdnav_describe_title_chapters(dvdnav_t *self, int32_t title, uint64_t **times, uint64_t *duration);
    for (int i = 0; i < nr_titles; i++) {
        uint64_t duration = 0;
        auto n = dvdnav_describe_title_chapters(handle, i, NULL, &duration);
        if (max_duration < duration) {
            max_duration = duration;
            //title 
        }
    }
#endif
    if (handle) dvdnav_close(handle);
    return QString::fromUtf8(title);

on_error:
    qWarning() << dvdnav_err_to_string(handle);
    if (handle) dvdnav_close(handle);
    return "";
}

}
}


