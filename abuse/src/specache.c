#include "specache.hpp"

spec_directory_cache sd_cache;

void spec_directory_cache::load(bFILE *fp)
{
  short tfn=fp->read_short();
  int i;
  unsigned char len;
  char fn[256];
  size=0;
  for (i=0;i<tfn;i++)
  {
    fp->read(&len,1);
    fp->read(fn,len);
    get_spec_directory(fn,fp);
  }  
}

void spec_directory_cache::save(bFILE *fp)
{
  int total=0,i;
  filename_node *f=fn_list;
  for (;f;f=f->next)
    total++;
  fp->write_short(total);
  for (f=fn_list;f;f=f->next)
  {
    unsigned char len=strlen(f->filename())+1;
    fp->write(&len,1);
    fp->write(f->filename(),len);
    f->sd->write(fp);
  }
}


spec_directory *spec_directory_cache::get_spec_directory(char *filename, bFILE *fp)
{
  filename_node **parent=0,*p=fn_root;
  while (p)
  {
    int cmp=strcmp(p->filename(),filename);
    if (cmp<0)
      parent=&p->left;
    else if (cmp>0)
      parent=&p->right;
    else 
      return p->sd;
    p=*parent;        
  }  
  
  int need_close=0;
  if (!fp)
  {
    fp=open_file(filename,"rb");
    if (fp->open_failure()) 
    {
      delete fp;
      return 0;
    }
    need_close=1;
  }
  
  filename_node *f=new filename_node(filename,new spec_directory(fp));
  f->next=fn_list;
  fn_list=f;

  size+=f->sd->size;
  if (parent)
    *parent=f;
  else
    fn_root=f;

  if (need_close)
    delete fp;
  return f->sd;
}

void spec_directory_cache::clear()
{
  size=0;
  clear(fn_root);
  fn_root=0;
}

void spec_directory_cache::clear(filename_node *f)
{
  if (f)
  {
    if (f->left)
    {
      clear(f->left);
      delete f->left;
    } 
    if (f->right)
    {
      clear(f->right);
      delete f->right;
    }
  }
}
