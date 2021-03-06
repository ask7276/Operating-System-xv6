// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.
//
// The implementation uses two state flags internally:
// * B_VALID: the buffer data has been read from the disk.
// * B_DIRTY: the buffer data has been modified
//     and needs to be written to disk.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "mmu.h"
#include "proc.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "fcntl.h"
#include "buf.h"
#include "file.h"

struct {
  struct spinlock lock;
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // head.next is most recently used.
  struct buf head;
} bcache;

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");

//PAGEBREAK!
  // Create linked list of buffers
  bcache.head.prev = &bcache.head;
  bcache.head.next = &bcache.head;
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    initsleeplock(&b->lock, "buffer");
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  acquire(&bcache.lock);

  // Is the block already cached?
  for(b = bcache.head.next; b != &bcache.head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached; recycle an unused buffer.
  // Even if refcnt==0, B_DIRTY indicates a buffer is in use
  // because log.c has modified it but not yet committed it.
  for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
    if(b->refcnt == 0 && (b->flags & B_DIRTY) == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->flags = 0;
      b->refcnt = 1;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if((b->flags & B_VALID) == 0) {
    iderw(b);
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  b->flags |= B_DIRTY;
  iderw(b);
}

// Release a locked buffer.
// Move to the head of the MRU list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  acquire(&bcache.lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }

  release(&bcache.lock);
}

struct inode*
create(char *path, short type, short major, short minor)
{
  uint off;
  struct inode *ip, *dp;
  char name[DIRSIZ];

  if((dp = nameiparent(path, name)) == 0)
    return 0;
  ilock(dp);

  if((ip = dirlookup(dp, name, &off)) != 0){
    iunlockput(dp);
    ilock(ip);
    if(type == T_FILE && ip->type == T_FILE)
      return ip;
    iunlockput(ip);
    return 0;
  }

  if((ip = ialloc(dp->dev, type)) == 0)
    panic("create: ialloc");

  ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  iupdate(ip);

  if(type == T_DIR){  
    dp->nlink++;  
    iupdate(dp);
    if(dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
      panic("create dots");
  }

  if(dirlink(dp, name, ip->inum) < 0)
    panic("create: dirlink");

  iunlockput(dp);

  return ip;
}


struct file*
createSwapFile(char *pg,int pid,pte_t *pte)
{
  // cprintf("Initiated\n");
  struct file* toret;
	char path[100];
  // ???<pid>_<VA[20:]>.swp
  // cprintf("XXXXXXXX%dXXXXXXXX\n",strlen(pg));
  uint x=((*pte)&(0xfffff000));
  x=(x>>12);
  itoa(pid,path);
  int len=strlen(path);
  path[len]='_';
  path[len+1]='\0';
  len=strlen(path);
	itoa(x, path+ len);
    begin_op();
    struct inode * in = create(path, 2, 0, 0);
	iunlock(in);
  toret=filealloc();
	if (toret == 0)
		panic("no slot for files on /store");
  
	toret->ip = in;
	toret->type = FD_INODE;
	toret->off = 0;
	toret->readable = O_WRONLY;
	toret->writable = O_RDWR;
    end_op();
  return toret;
}
/* Write 4096 bytes pg to the eight consecutive sectors
 * starting at blk.
 */
void
write_page_to_disk(uint dev, char *pg, uint blk,int pid,pte_t *pte)
{
  struct file* towrite=createSwapFile(pg,pid,pte);
  struct buf* buffer;
  int blockno=0;
  int ithPartOfPage=0;
  for(int i=0;i<8;i++){
    ithPartOfPage=i*512;
    blockno=blk+i;
    buffer=bget(ROOTDEV,blockno);
    /*
    Writing physical page to disk by dividing it into 8 pieces (4096 bytes/8 = 512 bytes = 1 block)
    As one page requires 8 disk blocks as given by 4096/512=8.
    */
    towrite->off=i*512;
    memmove(buffer->data,pg+ithPartOfPage,512);   
    filewrite(towrite,(char *)buffer,512);
    bwrite(buffer);
    brelse(buffer);                            
  }
}

/* Read 4096 bytes from the eight consecutive sectors
 * starting at blk into pg.
 */
void
read_page_from_disk(uint dev, char *pg, uint blk)
{
  struct buf* buffer;
  int blockno=0;
  int ithPartOfPage=0;
  for(int i=0;i<8;i++){
    ithPartOfPage=i*512;
    blockno=blk+i;
    buffer=bread(ROOTDEV,blockno);    //if present in buffer, returns from buffer else from disk
    memmove(pg+ithPartOfPage, buffer->data,512);  //write to pg from buffer
    brelse(buffer);                   //release lock
  }

}

//PAGEBREAK!
// Blank page.
