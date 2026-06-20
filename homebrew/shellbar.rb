class Shellbar < Formula
  desc "Ghostty-like terminal emulator with a configurable command toolbar"
  homepage "https://github.com/rendergraf/shellbar"
  url "https://github.com/rendergraf/shellbar/archive/refs/tags/v1.9.0.tar.gz"
  sha256 "SKIP"
  license "MIT"
  head "https://github.com/rendergraf/shellbar.git", branch: "main"

  depends_on "cmake" => :build
  depends_on "ninja" => :build
  depends_on "zig" => :build
  depends_on "pkg-config" => :build
  depends_on "gtk4"
  depends_on "libadwaita"
  depends_on "cairo"
  depends_on "pango"
  depends_on "gdk-pixbuf"
  depends_on "graphene"

  def install
    system "cmake", "-B", "build", "-G", "Ninja",
           "-DCMAKE_BUILD_TYPE=Release",
           "-DCMAKE_INSTALL_PREFIX=#{prefix}",
           *std_cmake_args
    system "cmake", "--build", "build"
    bin.install "build/shellbar"
  end

  test do
    system "#{bin}/shellbar", "--version"
  end
end
