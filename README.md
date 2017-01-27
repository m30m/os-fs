# OS Filesystem
Simple File System Abstraction

Overall architecture of sectors:

```
#-----Sector 0-----#
|    Magic Number  |
|  iNode   Bitmap  |
|-----Sector 1-----|
| Datablock Bitmap |
|        .         |
|        .         |
|-----Sector 4-----|
|  iNodes Blocks   |
|        .         |
|        .         |
|        .         |
|----Sector 254----|
|                  |
|    Empty Space!  |
|                  |
|----Sector 256----|
|                  |
|                  |
|    Data Blocks   |
|                  |
|                  |
#---End of Disk----#
```
