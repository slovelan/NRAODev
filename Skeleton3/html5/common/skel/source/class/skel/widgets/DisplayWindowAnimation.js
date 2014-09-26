/**
 * A display window specialized for controlling data animators.
 */

/**
 ************************************************************************ */

qx.Class
        .define(
                "skel.widgets.DisplayWindowAnimation",
                {
                    extend : skel.widgets.DisplayWindow,

                    /**
                     * Constructor.
                     */
                    construct : function(row, col, winId ) {
                        this.base(arguments, "animator", row, col, winId );
                        this.setLayout(new qx.ui.layout.VBox(5));
                        this.m_links = [];
                    },

                    members : {

                        /**
                         * Implemented to remove the title.
                         */
                        setPlugin : function(label) {
                            this._initDisplaySpecific();
                            arguments.callee.base.apply(this, arguments, label);
                            this.m_content.remove(this.m_title);
                        },

                        /**
                         * Adds or removes a specific animator from the display
                         * based on what the user has selected from the menu.
                         */
                        _showHideAnimation : function() {
                            for (var i = 0; i < this.m_checks.length; i++) {

                                var animId = this.m_checks[i].getLabel();
                                var animVisible = this.m_checks[i].getValue();
                                if (animVisible) {
                                    if (this.m_animators[animId] == null) {
                                        this.m_animators[animId] = new skel.boundWidgets.Animator(
                                                animId, this.m_pluginId, this
                                                        .getIdentifier());
                                    }
                                    if (this.indexOf(this.m_animators[animId]) == -1) {
                                        this.m_content
                                                .add(this.m_animators[animId]);
                                    }
                                } else {
                                    if (this.indexOf(this.m_animators[animId]) >= 0) {
                                        this.m_content
                                                .remove(this.m_animators[animId]);
                                    }
                                }
                            }
                        },

                        /**
                         * Returns animation specific menu buttons.
                         */
                        getWindowSubMenu : function() {
                            var windowMenuList = []

                            var animationButton = new qx.ui.toolbar.MenuButton(
                                    "Animation");
                            animationButton.setMenu(this._initShowMenuWindow());
                            windowMenuList.push(animationButton);
                            return windowMenuList;
                        },

                        /**
                         * Initializes animator specific items for the context
                         * menu.
                         */
                        _initDisplaySpecific : function() {
                            this.m_showAnimationButton = new qx.ui.menu.Button(
                                    "Animate");
                            this.m_showAnimationButton.setMenu(this
                                    ._initShowMenu());
                            this.m_contextMenu.add(this.m_showAnimationButton);
                        },

                        /**
                         * Initializes a menu for the different animators that
                         * can be displayed that is suitable for the main menu;
                         * corresponding check boxes are coordinated with the
                         * context menu.
                         */
                        _initShowMenuWindow : function() {
                            var showMenu = new qx.ui.menu.Menu;
                            for (var i = 0; i < this.m_supportedAnimations.length; i++) {
                                var animId = this.m_supportedAnimations[i];
                                var animCheck = new qx.ui.menu.CheckBox(animId);
                                showMenu.add(animCheck);
                                this.m_checks[i].bind("value", animCheck,
                                        "value");
                                animCheck.bind("value", this.m_checks[i],
                                        "value");
                            }
                            return showMenu;
                        },

                        /**
                         * Initializes the context menu for the different animators that can
                         * be displayed.
                         */
                        _initShowMenu : function() {
                            var showMenu = new qx.ui.menu.Menu;
                            var pathDict = skel.widgets.Path.getInstance();
                            for (var i = 0; i < this.m_supportedAnimations.length; i++) {
                                var animId = this.m_supportedAnimations[i];
                                this.m_checks[i] = new qx.ui.menu.CheckBox(
                                        animId);
                                showMenu.add(this.m_checks[i]);
                                this.m_checks[i].addListener("changeValue",
                                        function(ev) {
                                            this._showHideAnimation();
                                        }, this);
                                //Setting any animators visible that have existing shared variables.
                                var framePath = pathDict.BASE_PATH + this.m_pluginId+ pathDict.SEP + this.m_supportedAnimations[i].toLowerCase()+ pathDict.SEP + pathDict.FRAME_STEP + pathDict.SEP + this.m_identifier; 
                                var frameVar = this.m_connector.getSharedVar(framePath);
                                if (frameVar.get()){
                                    this.m_checks[i].setValue( true );
                                }
                            }
                            return showMenu;
                        },

                        m_showAnimationButton : null,
                        m_supportedAnimations : [ "Channel", "Image", "Region" ],
                        m_checks : [],
                        m_animators : {}
                    }

                });
