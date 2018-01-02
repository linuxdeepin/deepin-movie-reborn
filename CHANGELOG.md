##  3.2.0.1 (2018-01-02)


#### Bug Fixes

*   load from cached thumb stream ([b437995e](b437995e))



##  3.2.0 (2017-12-28)


#### Bug Fixes

*   minor changes ([dc9306ab](dc9306ab))
*   to leave progress bar when preview hide ([27b4c9a9](27b4c9a9))
*   url comparision and signleloop mode ([1bc0f553](1bc0f553))
*   handle context menu event correctly ([40b2e9ae](40b2e9ae))
*   optimize drag to resize process ([0e85688f](0e85688f))
*   use correct font to do text eliding ([172a3537](172a3537))
*   optimize drop handling ([d4a6bcac](d4a6bcac))
*   optimize _lastRectInNormalMode tracking ([b97246c9](b97246c9))
*   should init debug level before instantiation ([78d432e2](78d432e2))
*   make play state animation smooth ([d4f08feb](d4f08feb))
*   adjust process of toggleUIMode ([bded982f](bded982f))
*   check and emit an enter when necessary ([71ba3b88](71ba3b88))
*   remove over-detailed CMakeLists options ([ee9fad4e](ee9fad4e))
*   sliderMoved occasionally isn't signalled ([9b0d1047](9b0d1047))
*   potential conflict with kwin ([bd43d2df](bd43d2df))
*   Adapt lintian ([128cc039](128cc039))

#### Features

*   update shadow with focus change ([dac097c6](dac097c6))
*   support dxcb mode ([3c97643e](3c97643e))
*   quit fs to maximized state if it was ([fcf13d3f](fcf13d3f))
*   remember playlist position when quit ([c7d8fe1b](c7d8fe1b))
*   play state animation ([d2f68123](d2f68123))
*   dynamic slider expansion animation ([e89450f7](e89450f7))
* **dxcb:**
  *  smooth resizing ([d68be4e7](d68be4e7))
  *  better dxcb support ([b7171b1c](b7171b1c))



##  3.1.1 (2017-12-13)


#### Features

*   use better ElideText for movie info ([544ccc94](544ccc94))
*   improve preview and progress bar design ([f0c70499](f0c70499))
*   set debug log level for backend ([0315115b](0315115b))
*   disable maximization in mini mode ([0ebc63f2](0ebc63f2))
*   drag to restore ([06f20bfe](06f20bfe))

#### Bug Fixes

*   PreviewOnMouseover does not affect indicator ([8a685cc3](8a685cc3))
*   allow suspendToolsWindow on title area in fs ([804a52e5](804a52e5))
*   disable indicator on idle ([865a230e](865a230e))
*   external sub loading and titlbar state ([f2d85d2f](f2d85d2f))
*   reduce resize request ([8bb48f72](8bb48f72))
*   allow resumeToolsWindow when playlist opened ([f69873a2](f69873a2))
*   try fully encoded url first ([8fbde1fd](8fbde1fd))
*   dont restore to idle size in mini mode ([3b4cd454](3b4cd454))
*   make url dialog centered around main window ([20ded42e](20ded42e))
*   db and cache info init ([62fe66f5](62fe66f5))
* **workaround:**  bypass mouse event from other source ([077c110f](077c110f))



##  3.0.2 (2017-12-11)


#### Bug Fixes

*   switchPosition: reset _last if _current changed ([7448c66b](7448c66b))
*   sync to save global state ([a99f2003](a99f2003))
*   dont popup context menu from titlebar ([ab8bea72](ab8bea72))
*   update _current after position switch ([892c2e75](892c2e75))
*   workaround a drop indicator issue ([abf87d9e](abf87d9e))
*   disable panscan in fs or maximized state ([3e04bd11](3e04bd11))
*   take care filename input format ([6605e277](6605e277))
*   on restore to default in normal state ([4d2377bd](4d2377bd))
*   wrong conditional compilation ([5e69c0cc](5e69c0cc))
*   use two variables to track mouse state ([27087d03](27087d03))
*   adjust popup according to TOOLBOX_TOP_EXTENT ([179b5ae2](179b5ae2))
*   delay toggling playlist when return to fs ([bd30d9b6](bd30d9b6))
*   expand playlist into invisble extent of toolbox ([76216161](76216161))
*   correct drag to maximize behaviour ([96d74eed](96d74eed))
*   a few minor changes ([eb7bd68f](eb7bd68f))

#### Features

*   primitive caching scheme for playlist items ([30161b36](30161b36))
*   show unique icon for different kinds of urls ([53586c03](53586c03))
*   support playlist item repositioning ([1fcd1c26](1fcd1c26))
*   reset last valid size when new vidoe loaded ([e6eaa49b](e6eaa49b))
*   honor video metadata for rotation ([71f5d4e5](71f5d4e5))
*   adjust playlist geometry when toggling fs ([f6d10a90](f6d10a90))
*   remember both size and pos of last spot ([5d62d0f4](5d62d0f4))
*   only show size notif for manual resizing ([ec7bf9f3](ec7bf9f3))
*   sync above state with wm ([db49fa40](db49fa40))
*   hide popup when turn to inactive state ([1f294ef7](1f294ef7))
*   expand slider response area even wider ([f703dce2](f703dce2))
*   restore to default size when idle ([fce147c2](fce147c2))
*   make response area of slider look wider ([aca6eefd](aca6eefd))
*   expand progress response area ([bb6f3f92](bb6f3f92))
*   support libdmr.pc ([e16a3944](e16a3944))
*   enable libdmr to override composite mode ([cfd1058e](cfd1058e))
*   make video full the whole framebuffer ([1fcb45f3](1fcb45f3))
*   libdmr interface usage demo ([88d9654b](88d9654b))
*   split core function into libdmr ([c37f3dc1](c37f3dc1))



##  3.0.1 (2017-11-27)


#### Bug Fixes

*   potential crash when closing a smb shared video ([0318ec7e](0318ec7e))

#### Features

*   update playlist geometry under flatpak env ([ecf257c9](ecf257c9))



##  3.0 (2017-11-21)


#### Features

*   make sub font size approximate pixel size ([977f6733](977f6733))



##  2.9.98 (2017-11-21)


#### Bug Fixes

*   improve 4k playback by disable opengl-hq ([d3f222a7](d3f222a7))
*   update svgs and more QImageReader migration ([93fa0caa](93fa0caa))

#### Features

*   make play state button clickable ([26b25be8](26b25be8))
*   hide titlebar in fullscreen playback ([1a86fbd3](1a86fbd3))
*   render HiDPI texture by QImageReader ([2dfe3c0d](2dfe3c0d))
*   use QImageReader to render HiDPI images ([a8afaa64](a8afaa64))



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


