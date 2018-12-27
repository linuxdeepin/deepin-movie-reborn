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
#include "utils.h"
#include <QtDBus>
#include <QtWidgets>

namespace dmr {
namespace utils {
using namespace std;

void ShowInFileManager(const QString &path)
{
    if (path.isEmpty() || !QFile::exists(path)) {
        return;
    }

    QUrl url = QUrl::fromLocalFile(QFileInfo(path).dir().absolutePath());
    QUrlQuery query;
    query.addQueryItem("selectUrl", QUrl::fromLocalFile(path).toString());
    url.setQuery(query);

    qDebug() << __func__ << url.toString();

    // Try dde-file-manager
    QProcess *fp = new QProcess();
    QObject::connect(fp, SIGNAL(finished(int)), fp, SLOT(deleteLater()));
    fp->start("dde-file-manager", QStringList(url.toString()));
    fp->waitForStarted(3000);

    if (fp->error() == QProcess::FailedToStart) {
        // Start dde-file-manager failed, try nautilus
        QDBusInterface iface("org.freedesktop.FileManager1",
                "/org/freedesktop/FileManager1",
                "org.freedesktop.FileManager1",
                QDBusConnection::sessionBus());
        if (iface.isValid()) {
            // Convert filepath to URI first.
            const QStringList uris = { QUrl::fromLocalFile(path).toString() };
            qDebug() << "freedesktop.FileManager";
            // StartupId is empty here.
            QDBusPendingCall call = iface.asyncCall("ShowItems", uris, "");
            Q_UNUSED(call);
        }
        // Try to launch other file manager if nautilus is invalid
        else {
            qDebug() << "desktopService::openUrl";
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).dir().absolutePath()));
        }
        fp->deleteLater();
    }
}


static int min(int v1, int v2, int v3) 
{
    return std::min(v1, std::min(v2, v3));
}

static int stringDistance(const QString& s1, const QString& s2) {
    int n = s1.size(), m = s2.size();
    if (!n || !m) return max(n, m);

    vector<int> dp(n+1);
    for (int i = 0; i < n+1; i++) dp[i] = i;
    int pred = 0;
    int curr = 0;

    for (int i = 0; i < m; i++) {
        dp[0] = i;
        pred = i+1;
        for (int j = 0; j < n; j++) {
            if (s1[j] == s2[i]) {
                curr = dp[j];
            } else {
                curr = min(dp[j], dp[j+1], pred) + 1;
            }
            dp[j] = pred;
            pred = curr;

        }
        dp[n] = pred;
    }

    return curr;
}

bool IsNamesSimilar(const QString& s1, const QString& s2)
{
    auto dist = stringDistance(s1, s2);
    return (dist >= 0 && dist <= 4); //TODO: check ext.
}

QFileInfoList FindSimilarFiles(const QFileInfo& fi)
{
    QFileInfoList fil;

    QDirIterator it(fi.absolutePath());
    while (it.hasNext()) {
        it.next();
        if (!it.fileInfo().isFile()) {
            continue;
        }

        if (IsNamesSimilar(fi.fileName(), it.fileInfo().fileName())) {
            fil.append(it.fileInfo());
        }
        
    }

    //struct {
        //bool operator()(const QFileInfo& fi1, const QFileInfo& fi2) const {
            //return CompareNames(fi1.fileName(), fi2.fileName());
        //}
    //} SortByDigits;
    //std::sort(fil.begin(), fil.end(), SortByDigits);
    return fil;
}

bool CompareNames(const QString& fileName1, const QString& fileName2) 
{
    static QRegExp rd("\\d+");
    int pos = 0;
    while ((pos = rd.indexIn(fileName1, pos)) != -1) {
        auto inc = rd.matchedLength();
        auto id1 = fileName1.midRef(pos, inc);

        auto pos2 = rd.indexIn(fileName2, pos);
        if (pos == pos2) {
            auto id2 = fileName2.midRef(pos, rd.matchedLength());
            //qDebug() << "id compare " << id1 << id2;
            if (id1 != id2) {
                bool ok1, ok2;
                bool v = id1.toInt(&ok1) < id2.toInt(&ok2);
                if (ok1 && ok2) return v;
                return id1.localeAwareCompare(id2) < 0;
            }
        }

        pos += inc;
    }
    return fileName1.localeAwareCompare(fileName2) < 0;
}

// hash the whole file takes amount of time, so just pick some areas to be hashed
QString FastFileHash(const QFileInfo& fi)
{
    auto sz = fi.size();
    QList<qint64> offsets = {
        4096,
        sz - 8192
    };

    QFile f(fi.absoluteFilePath());
    if (!f.open(QFile::ReadOnly)) {
        return QString();
    }

    if (fi.size() < 8192) {
        auto bytes = f.readAll();
        return QString(QCryptographicHash::hash(bytes, QCryptographicHash::Md5).toHex());
    }

    QByteArray bytes;
    std::for_each(offsets.begin(), offsets.end(), [&bytes, &f](qint64 v) {
        f.seek(v);
        bytes += f.read(4096);

    });

    return QString(QCryptographicHash::hash(bytes, QCryptographicHash::Md5).toHex());
}

// hash the entire file (hope file is small)
QString FullFileHash(const QFileInfo& fi)
{
    auto sz = fi.size();

    QFile f(fi.absoluteFilePath());
    if (!f.open(QFile::ReadOnly)) {
        return QString();
    }

    auto bytes = f.readAll();
    return QString(QCryptographicHash::hash(bytes, QCryptographicHash::Md5).toHex());
}

QPixmap MakeRoundedPixmap(QPixmap pm, qreal rx, qreal ry, int rotation)
{
    auto dpr = pm.devicePixelRatio();
    QPixmap dest(pm.size());
    dest.setDevicePixelRatio(dpr);

    auto scaled_rect = QRectF({0, 0}, QSizeF(dest.size() / dpr));
    dest.fill(Qt::transparent);

    QPainter p(&dest);
    p.setRenderHints(QPainter::Antialiasing|QPainter::SmoothPixmapTransform);

    QPainterPath path;
    path.addRoundedRect(QRect(QPoint(), scaled_rect.size().toSize()), rx, ry);
    p.setClipPath(path);

    QTransform transform;
    transform.translate(scaled_rect.width()/2, scaled_rect.height()/2);
    transform.rotate(rotation);
    transform.translate(-scaled_rect.width()/2, -scaled_rect.height()/2);
    p.setTransform(transform);

    p.drawPixmap(scaled_rect.toRect(), pm);

    return dest;
}

QPixmap MakeRoundedPixmap(QSize sz, QPixmap pm, qreal rx, qreal ry, qint64 time)
{
    auto dpr = pm.devicePixelRatio();
    QPixmap dest(sz);
    dest.setDevicePixelRatio(dpr);
    dest.fill(Qt::transparent);

    auto scaled_rect = QRectF({0, 0}, QSizeF(dest.size() / dpr));

    QPainter p(&dest);
    p.setRenderHints(QPainter::Antialiasing|QPainter::SmoothPixmapTransform);

    p.setPen(QColor(0, 0, 0, 255 / 10));
    p.drawRoundedRect(scaled_rect, rx, ry);

    QPainterPath path;
    auto r = scaled_rect.marginsRemoved({1, 1, 1, 1});
    path.addRoundedRect(r, rx, ry);
    p.setClipPath(path);
    p.drawPixmap(1, 1, pm);


    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    QFont ft;
    ft.setPixelSize(12);
    ft.setWeight(QFont::Medium);
    p.setFont(ft);

    auto tm_str = QTime(0, 0, 0).addSecs(time).toString("hh:mm:ss");
    QRect bounding = QFontMetrics(ft).boundingRect(tm_str);
    bounding.moveTopLeft({((int)(dest.width() / dpr)) - 5 - bounding.width(),
            ((int)(dest.height() / dpr)) - 5 - bounding.height()});

    {
        QPainterPath pp;
        pp.addText(bounding.bottomLeft() + QPoint{0, 1}, ft, tm_str);
        QPen pen(QColor(0, 0, 0, 50));
        pen.setWidth(2);
        p.setBrush(QColor(0, 0, 0, 50));
        p.setPen(pen);
        p.drawPath(pp);
    }

    {
        QPainterPath pp;
        pp.addText(bounding.bottomLeft(), ft, tm_str);
        p.fillPath(pp, QColor(Qt::white));
    }

    return dest;
}

uint32_t InhibitStandby()
{
    QDBusInterface iface("org.freedesktop.ScreenSaver",
                         "/org/freedesktop/ScreenSaver",
                         "org.freedesktop.ScreenSaver");
    QDBusReply<uint32_t> reply = iface.call("Inhibit", "deepin-movie", "playing in fullscreen");

    if (reply.isValid()) {
        return reply.value();
    }

    qDebug() << reply.error().message();
    return 0;
}

void UnInhibitStandby(uint32_t cookie)
{
    QDBusInterface iface("org.freedesktop.ScreenSaver",
                         "/org/freedesktop/ScreenSaver",
                         "org.freedesktop.ScreenSaver");
    iface.call("UnInhibit", cookie);
}

uint32_t InhibitPower()
{
    QDBusInterface iface("org.freedesktop.PowerManagement",
                         "/org/freedesktop/PowerManagement",
                         "org.freedesktop.PowerManagement");
    QDBusReply<uint32_t> reply = iface.call("Inhibit", "deepin-movie", "playing in fullscreen");

    if (reply.isValid()) {
        return reply.value();
    }

    qDebug() << reply.error().message();
    return 0;
}

void UnInhibitPower(uint32_t cookie)
{
    QDBusInterface iface("org.freedesktop.PowerManagement",
                         "/org/freedesktop/PowerManagement",
                         "org.freedesktop.PowerManagement");
    iface.call("UnInhibit", cookie);
}

void MoveToCenter(QWidget* w)
{
    QDesktopWidget *dw = QApplication::desktop();
    QRect r = dw->availableGeometry(w);

    w->move(r.center() - w->rect().center());
}

QString Time2str(qint64 seconds)
{
    QTime d(0, 0, 0);
    d = d.addSecs(seconds);
    return d.toString("hh:mm:ss");
}

bool ValidateScreenshotPath(const QString& path)
{
    auto name = path.trimmed();
    if (name.isEmpty()) return false;

    if (name.size() && name[0] == '~') {
        name.replace(0, 1, QDir::homePath());
    }

    QFileInfo fi(name);
    if (fi.exists()) {
        if (!fi.isDir()) {
            return false;
        }

        if (!fi.isReadable() || !fi.isWritable()) {
            return false;
        }
    }

    return true;
}

QImage LoadHiDPIImage(const QString& filename)
{
    QImageReader reader(filename);
    reader.setScaledSize(reader.size() * qApp->devicePixelRatio());
    auto img =  reader.read();
    img.setDevicePixelRatio(qApp->devicePixelRatio());
    return img;
}

QPixmap LoadHiDPIPixmap(const QString& filename)
{
    return QPixmap::fromImage(LoadHiDPIImage(filename));
}

QString ElideText(const QString &text, const QSize &size,
        QTextOption::WrapMode wordWrap, const QFont &font,
        Qt::TextElideMode mode, int lineHeight, int lastLineWidth)
{
    int height = 0;

    QTextLayout textLayout(text);
    QString str;
    QFontMetrics fontMetrics(font);

    textLayout.setFont(font);
    const_cast<QTextOption*>(&textLayout.textOption())->setWrapMode(wordWrap);

    textLayout.beginLayout();

    QTextLine line = textLayout.createLine();

    while (line.isValid()) {
        height += lineHeight;

        if(height + lineHeight >= size.height()) {
            str += fontMetrics.elidedText(text.mid(line.textStart() + line.textLength() + 1),
                    mode, lastLineWidth);

            break;
        }

        line.setLineWidth(size.width());

        const QString &tmp_str = text.mid(line.textStart(), line.textLength());

        if (tmp_str.indexOf('\n'))
            height += lineHeight;

        str += tmp_str;

        line = textLayout.createLine();

        if(line.isValid())
            str.append("\n");
    }

    textLayout.endLayout();

    if (textLayout.lineCount() == 1) {
        str = fontMetrics.elidedText(str, mode, lastLineWidth);
    }

    return str;
}

}
}
