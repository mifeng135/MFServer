jemalloc
git clone https://github.com/jemalloc/jemalloc.git
cd jemalloc
./autogen.sh
./configure --enable-shared --enable-prof --enable-stats
--enable-shared: 构建动态库（默认也会构建静态库）
--disable-static: 只构建动态库（不构建静态库）
--enable-prof: 启用内存分析功能
--enable-stats: 启用统计功能
--prefix=/usr/local/lib: 指定安装路径
make -j8
make install



sudo apt install libjsoncpp-dev
sudo apt install uuid-dev
sudo apt install zlib1g-dev
sudo apt install openssl libssl-dev (可选)

drogon 
unzip drogon-1.9.11
cd drogon-1.9.11
mkdir build
cd build 
cmake -DCMAKE_BUILD_TYPE=Release -DTRANTOR_USE_TLS=none ..
make -j8
make install

lua-5.4.3
windows
cmake -B build -S .
cmake --build build --config Release

liunx macos
cd lua-5.4.3
make
make install


nohup ./MFServer > /dev/null 2>&1 &
cmake -DCMAKE_BUILD_TYPE=Debug ..

trantor-1.5.26 编译
cd build

windows 
cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON -DTRANTOR_USE_TLS=none ..
cmake --build .

liunx
cmake -DCMAKE_BUILD_TYPE=Release -DTRANTOR_USE_TLS=none ..
make -j8


