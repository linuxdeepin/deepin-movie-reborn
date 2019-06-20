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
#ifndef _DMR_COMPOSITING_MANAGER
#define _DMR_COMPOSITING_MANAGER 

#include <QtCore>
#include <string>
#include <vector>

namespace dmr {

enum Platform {
    Unknown,
    X86,  // intel & amd
    Mips, // loongson
    Alpha, // sunway
    Arm64
};

enum OpenGLInteropKind {
    INTEROP_NONE,
    INTEROP_VAAPI_EGL,
    INTEROP_VAAPI_GLX,
    INTEROP_VDPAU_GLX,
};

using PlayerOption = QPair<QString, QString>;
using PlayerOptionList = QList<PlayerOption>;

class CompositingManager: public QObject {
    public:
        static CompositingManager& get();
        virtual ~CompositingManager();

        /**
         * should call this before any other qt functions get exec'ed.
         * this makes sure mpv openglcb-interop to work correctly
         */
        static void detectOpenGLEarly();
        /**
         * get detectOpenGLEarly result
         */
        static OpenGLInteropKind interopKind();

        /**
         * override auto-detected compositing state.
         * should call this right before player engine gets instantiated.
         */
        void overrideCompositeMode(bool useCompositing);

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
        bool isProprietaryDriver();

        bool is_device_viable(int id);
        bool is_card_exists(int id, const std::vector<std::string>& drivers);

        bool _composited {false};
        Platform _platform {Platform::Unknown};
};
}

#endif /* ifndef _DMR_COMPOSITING_MANAGER */

