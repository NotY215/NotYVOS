NOTYVOS_XMB_VERSION = local
NOTYVOS_XMB_SITE = $(BR2_EXTERNAL_NOTYVOS_PATH)/../src/xmb
NOTYVOS_XMB_SITE_METHOD = local
NOTYVOS_XMB_DEPENDENCIES = sdl2 sdl2_ttf sdl2_image host-pkgconf
$(eval $(cmake-package))
