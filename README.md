# Allocator 
Custom dynamic memory allocator using the segragated fit mechanism

Built this as part of [SiKV](https://github.com/misachi/SiKV), that I am also working on, but felt the need to have it separate to work on it independently -- as I add more features in the future

# Installation
```
Clone the repository to a local directory e.g /tmp

cd /tmp/allocator  # Replace /tmp to your directory

make install
```

Once installed the header file can be accessed in the usual way: `<alloc.h>`

# Uninstallation
```
cd /tmp/allocator   # Replace /tmp to your directory
make uninstall
```
...or

```
rm /usr/local/lib/alloc.so
rm /usr/local/include/alloc.h
```

