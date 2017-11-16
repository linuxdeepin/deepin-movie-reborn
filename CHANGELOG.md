##  2.9.97 (2017-11-16)


#### Bug Fixes

*   correct shape mask in non-composited mode ([86397bf2](86397bf2))

#### Features

*   use vaapi_egl opengl interop when possible ([632afdf9](632afdf9))
*   support build with DTK_DMAN_PORTAL ([05a19b86](05a19b86))
*   support dman activation from flatpak env ([eb548afc](eb548afc))

##  2.9.96 (2017-11-09)


#### Bug Fixes

*   check cookie before inhibit ([573ba280](573ba280))
*   cookie should be unsigned int ([220231a6](220231a6))
*   check and do UnInhibit when window closed ([81110c29](81110c29))



## 2.9.95 (2017-11-02)


#### Bug Fixes

*   pixmap take device ratio into account ([f6b851e4](f6b851e4))
*   make toolbox more transparent ([06174b58](06174b58))

#### Features

*   scale fbo according to devicePixelRatio ([03307290](03307290))
*   honor devicePixelRatio for pixmap rendering ([e0390a35](e0390a35))
*   basic hdpi texture adaption ([86e09a82](86e09a82))
*   support flatpak ([842b8a7b](842b8a7b))
*   respect devicePixelRatio when move ([373f48ce](373f48ce))


