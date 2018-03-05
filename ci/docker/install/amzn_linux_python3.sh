set -ex
pushd .
wget -nv https://bootstrap.pypa.io/get-pip.py
mkdir py3
cd py3
wget -nv https://www.python.org/ftp/python/3.5.2/Python-3.5.2.tgz
tar -xvzf Python-3.5.2.tgz
cd Python-3.5.2
yum install -y zlib-devel openssl-devel sqlite-devel bzip2-devel gdbm-devel ncurses-devel xz-devel readline-devel
./configure --prefix=/opt/ --with-zlib-dir=/usr/lib64
make -j$(nproc)
mkdir /opt/bin
mkdir /opt/lib
make install
ln -s -f /opt/bin/python3 /usr/bin/python3
cd ../..
python3 get-pip.py
ln -s -f /opt/bin/pip /usr/bin/pip3

mkdir -p ~/.local/lib/python3.5/site-packages/
pip3 install numpy
popd