If my memory serves me (in my old age) the breakdown
of work goes as follows

Justin:

    csc452_mkdir

    csc452_rmdir

    csc452_mknod


Cristal:

    csc452_write

    csc452_read

    csc452_open

*Note: Be sure to "format" your disk after each pull
    
    dd bs=1K count=5K if=/dev/zero of=.disk
