# Maintainer: Desvelao <iamdesvelao@gmail.com>
#
# NOTE: sha256sums is still SKIP — fill in the real hash from the tagged
# source tarball before submitting this to the AUR.

pkgname=xfce4-journal-plugin
pkgver=0.1.0
pkgrel=1
pkgdesc="XFCE panel plugin for adding dated entries to one or more markdown journals"
arch=('x86_64')
url="https://github.com/Desvelao/xfce4-journal-plugin"
license=('MIT')
depends=('gtk3' 'xfce4-panel' 'jansson')
makedepends=('meson' 'ninja' 'pkgconf')
source=("$pkgname-$pkgver.tar.gz::$url/archive/v$pkgver.tar.gz")
sha256sums=('SKIP')

build() {
  cd "$pkgname-$pkgver"
  meson setup build --prefix=/usr --buildtype=release
  meson compile -C build
}

check() {
  cd "$pkgname-$pkgver"
  meson test -C build
}

package() {
  cd "$pkgname-$pkgver"
  meson install -C build --destdir "$pkgdir"
}
