#include "chars.hpp"
#include "game.hpp"
#include "intsect.hpp"
#include "lisp.hpp"
#include "jwindow.hpp"
#include "input.hpp"
#include "id.hpp"
#include "clisp.hpp"
#include "dprint.hpp"
#include "lisp_gc.hpp"

#define FADING_FRAMES 26
#define FADING_MAX 32

character_type **figures;



int total_weapons=0;
int *weapon_types=NULL;    // maps 0..total_weapons into 'real' weapon type

char *state_names[MAX_STATE]= {
			       "dead",
                               "dieing",
			       "stopped",
			       "start_run_jump","run_jump","run_jump_fall","end_run_jump",
			       "flinch_up","flinch_down",
			       "morph_pose",
			       "running" };

/*			       "start_still_jump","still_jump","still_jump_fall","end_still_jump",

			       "morph_pose",
			       
			       "walking",
			       

			       "weapon_draw",
			       "weapon_put_away",
			       "weapon_fire",
			       "weapon_end_fire",
			       "weapon_fire_up",
			       "weapon_end_fire_up",

			       "blocking",
			       "block_recoil",

			       "turn_around",
			       "flinch_up","flinch_down","flinch_back",
			       "flinch_air","flinch_ground","flinch_getup","woze",
			       "stance"
			     } ; */


char *cflag_names[TOTAL_CFLAGS]={"hurt_all","is_weapon","stoppable","can_block",
				 "hurtable","pushable","unlistable",
			         "add_front","cached_in","need_cache_in","unactive_shield"};
char *ofun_names[TOTAL_OFUNS]={"ai_fun","move_fun","draw_fun","map_draw_fun","damage_fun",
			       "next_state_fun","user_fun",
			       "constructor","reload_fun","get_cache_list_fun",
			       "type_change_fun"};

int character_type::add_state(void *symbol)             // returns index into seq to use
{
  if (item_type(symbol)!=L_SYMBOL)
  {
    lprint(symbol);
    lbreak("is not a symbol (in def_char)");
    exit(0);
  }

  void *val=symbol_value(symbol);

  int num;
  if (DEFINEDP(val))
  {
    if (item_type(val)!=L_NUMBER)
    {
      lprint(symbol);
      dprintf("expecting symbol value to be a number, instead got : ");
      lprint(val);
      lbreak("");
      exit(0);
    }
    num=lnumber_value(val);
  } else 
  {
    num=ts;
    if (num<MAX_STATE) num=MAX_STATE;
    int sp=current_space;
    current_space=PERM_SPACE;
    set_symbol_number(symbol,num);
    current_space=sp;
  }

  if (num<ts && seq[num])
  {
    lprint(symbol);
    lbreak("symbol has been assigned value %d, but value already in use by state %s\n"
	   "use a different symbol for this state\n",
	   lstring_value(symbol_name(seq_syms[num])));
    exit(0);
  } else if (num>=ts)
  {
    seq=(sequence **)jrealloc(seq,sizeof(sequence *)*(num+1),"state list");    
    seq_syms=(void **)jrealloc(seq_syms,sizeof(void *)*(num+1),"state sym list");    

    memset(&seq[ts],0,sizeof(sequence *)*((num+1)-ts));
    memset(&seq_syms[ts],0,sizeof(void *)*((num+1)-ts));

    ts=num+1;
  }

  seq_syms[num]=symbol;
  return num;
}

int flinch_state(character_state state)
{
  if (state==flinch_up || state==flinch_down)
    return 1;
  else return 0;
}

int character_type::cache_in()    // returns false if out of cache memory
{
  int i;
  if (get_cflag(CFLAG_CACHED_IN))
    return 1;
  cflags|=1<<CFLAG_CACHED_IN;

  for (i=0;i<ts;i++)
  {
    if (seq[i])
      seq[i]->cache_in();
  }
  return 1;
}

void character_type::add_sequence(character_state which, sequence *new_seq)
{
  if (seq[which])
    delete seq[which];
  seq[which]=new_seq;  
}  

void *l_obj_get(long number) // exten lisp function switches on number
{
  character_type *t=figures[current_object->otype];
  if (t->tiv<=number || !t->vars[number])
  {
    lbreak("access : variable does not exsist for this class\n");
    return 0;
  }
  return new_lisp_number(current_object->lvars[t->var_index[number]]);  
}

void l_obj_set(long number, void *arg)  // exten lisp function switches on number
{
  character_type *t=figures[current_object->otype];
  if (t->tiv<=number || !t->vars[number])
  {
    lbreak("set : variable does not exsist for this class\n");
    return;
  }
  current_object->lvars[t->var_index[number]]=lnumber_value(arg);
}

void l_obj_print(long number)  // exten lisp function switches on number
{
  character_type *t=figures[current_object->otype];
  if (t->tiv<=number || !t->vars[number])
  {
    lbreak("access : variable does not exsist for this class\n");
    return;
  }
  dprintf("%d",current_object->lvars[t->var_index[number]]);  
}

void character_type::add_var(void *symbol, void *name)
{
  /* First see if the variable has been defined for another object
     if so report a conflict if any occur */
  lisp_symbol *s=(lisp_symbol *)symbol;
  if (DEFINEDP(s->value) && (item_type(s->value)!=L_OBJECT_VAR))
  {
    lprint(symbol);
    lbreak("symbol already has a value, cannot instantiate an object varible");
    exit(0);
  } else if (DEFINEDP(s->value))
  {
    int index=((lisp_object_var *)s->value)->number;
    if (index<tiv)
    {
      if (vars[index])
      {       
	lbreak("While defining object %s :\n"
	       "  var '%s' was previously defined by another\n"
	       "  with index %d, but %s has a var listed '%s' with same index\n"
	       "  try moving definition of %s before previously declared object",
	       lstring_value(symbol_name(name)),
	       lstring_value(symbol_name(symbol)),
	       index,
	       lstring_value(symbol_name(name)),
	       lstring_value(symbol_name(vars[index])),
	       lstring_value(symbol_name(name))
	       );
	exit(0);
      } else 
      {
	var_index[index]=tv;
	vars[index]=symbol;
	tv++;
      }
    } else
    {
      int new_total=index+1;
      vars=(void **)jrealloc(vars,sizeof(void *)*new_total,"variable name list");
      var_index=(short *)jrealloc(var_index,sizeof(short)*new_total,"variable index");
      memset(&vars[tiv],0,(new_total-tiv)*sizeof(void *));
      memset(&var_index[tiv],0,(new_total-tiv)*sizeof(short));
      tiv=new_total;

      var_index[index]=tv;
      vars[index]=symbol;
      tv++;
    }    
  } else  /** Nope, looks like we have to add the variable ourself and define the assesor funs */
  {
    /* locate a free index in the index list */
    int free_index=tiv;
    for (int i=0;i<tiv;i++)
      if (!vars[i] && i<tiv) free_index=i;
    if (free_index==tiv)
    {
      int new_total=free_index+1;
      vars=(void **)jrealloc(vars,sizeof(void *)*new_total,"variable name list");
      var_index=(short *)jrealloc(var_index,sizeof(short)*new_total,"variable index");
      memset(&vars[tiv],0,(new_total-tiv)*sizeof(void *));
      memset(&var_index[tiv],0,(new_total-tiv)*sizeof(short));
      tiv=new_total;
    }

    /* create the var and add to var list */
    int sp=current_space;
    current_space=PERM_SPACE;

    add_c_object(symbol,free_index);
   
    vars[free_index]=symbol;
    var_index[free_index]=tv;
    tv++;
    current_space=sp;
  }
}


long character_type::isa_var_name(char *name)
{
  int i=0;
  for (;i<TOTAL_OBJECT_VARS;i++)
  {
    if (!strcmp(object_descriptions[i].name,name))
      return 1;
  }
  for (i=0;i<tiv;i++)
    if (!strcmp(lstring_value(symbol_name(vars[i])),name)) 
      return 1;
  return 0;
}

character_type::character_type(void *args, void *name) 
{
  p_ref r2(args);
  int i;
  ts=tv=0;
  seq=NULL;
  seq_syms=NULL;
  vars=NULL;
  var_index=NULL;
  tiv=0;

  void *l_abil=  make_find_symbol("abilities");
  void *l_funs=  make_find_symbol("funs");
  void *l_states=make_find_symbol("states");
  void *l_flags= make_find_symbol("flags");
  void *l_range= make_find_symbol("range");
  void *l_draw_range= make_find_symbol("draw_range");
  void *l_fields=make_find_symbol("fields");
  void *l_logo=  make_find_symbol("logo");
  void *l_vars=  make_find_symbol("vars");

  memset(fun_table,0,sizeof(fun_table));     // destory all hopes of fun
  fields=NULL;
  cflags=0;
  morph_mask=-1;
  morph_power=0;
  total_fields=0;
  logo=-1;
  rangex=rangey=0;
  draw_rangex=draw_rangey=0;

  for (i=0;i<TOTAL_ABILITIES;i++)
    abil[i]=get_ability_default((ability)i);          
  void *field=args;
  p_ref r7(field);
  for (;field;field=CDR(field))
  {
    void *f=CAR(CAR(field));
    p_ref r1(f);
    
    if (f==l_abil)
    {
      void *l=CDR(CAR(field));
      p_ref r4(l);
      for (i=0;i<TOTAL_ABILITIES;i++)
      {
	Cell *ab=assoc(make_find_symbol(ability_names[i]),l);
	p_ref r5(ab);
	if (!NILP(ab))
	  abil[i]=lnumber_value(eval(lcar(lcdr(ab))));
      }
    } else if (f==l_funs)
    {
      void *l=CDR(CAR(field));
      p_ref r4(l);
      for (i=0;i<TOTAL_OFUNS;i++)
      {
	Cell *ab=assoc(make_find_symbol(ofun_names[i]),l);
	p_ref r5(ab);
	if (!NILP(ab) && lcar(lcdr(ab)))
	fun_table[i]=lcar(lcdr(ab));
      }
    } else if (f==l_flags)
    {
      void *l=CDR(CAR(field));
      p_ref r4(l);
      for (i=0;i<TOTAL_CFLAGS;i++)
      {
	Cell *ab=assoc(make_find_symbol(cflag_names[i]),l);
	p_ref r5(ab);
	if (!NILP(ab) && eval(lcar(lcdr(ab))))
	cflags|=(1<<i);
      }
      
      if (get_cflag(CFLAG_IS_WEAPON))  // if this is a weapon add to weapon array
      {
	total_weapons++;
	weapon_types=(int *)jrealloc(weapon_types,sizeof(int)*total_weapons,"weapon map");
	weapon_types[total_weapons-1]=total_objects;
      }
    } else if (f==l_range)
    {
      rangex=lnumber_value(eval(lcar(lcdr(lcar(field)))));
      rangey=lnumber_value(eval(lcar(lcdr(lcdr(lcar(field))))));
    } else if (f==l_draw_range)
    {
      draw_rangex=lnumber_value(eval(lcar(lcdr(lcar(field)))));
      draw_rangey=lnumber_value(eval(lcar(lcdr(lcdr(lcar(field))))));
    } else if (f==l_states)
    {
      void *l=CDR(CAR(field));
      p_ref r4(l);
      char fn[100];
      strcpy(fn,lstring_value(eval(CAR(l)))); l=CDR(l);
      while (l)
      {
	seq[add_state(CAR((CAR(l))))]=new sequence(fn,eval(CAR(CDR(CAR(l)))),NULL);
	l=CDR(l);
      } 
    } else if (f==l_fields)
    {
      void *mf=CDR(CAR(field));
      p_ref r4(mf);
      while (!NILP(mf))
      {
	char *real=lstring_value(eval(lcar(lcar(mf))));
	char *fake=lstring_value(eval(lcar(lcdr(lcar(mf))))); 
	if (!isa_var_name(real))
	{
	  lprint(field);
	  lbreak("fields : no such var name \"%s\"\n",name);
	  exit(0);
	} 
	total_fields++;

	fields=(named_field **)jrealloc(fields,sizeof(named_field *)*total_fields,"named_fields");
	fields[total_fields-1]=new named_field(real,fake);
	mf=lcdr(mf);
      }
    } else if (f==l_logo)
    {
      char *fn=lstring_value(eval(CAR(CDR(CAR(field)))));
      char *o=lstring_value(eval(CAR(CDR(CDR(CAR(field))))));
      logo=cash.reg(fn,o,SPEC_IMAGE,1);
    } else if (f==l_vars)
    {
      void *l=CDR(CAR(field));
      p_ref r8(l);
      while (l)
      {
	add_var(CAR(l),name);
	l=CDR(l);
      }       
    }
    else
    {
      lprint(lcar(field));
      lbreak("Unknown field for character definiltion");
      exit(0);
    }    
  }

  if (!seq[stopped])
    lbreak("object (%s) has no stopped state, please define one!\n",
	   lstring_value(symbol_name(name)));

/*  char *fn=lstring_value(lcar(desc));
  if (!fn)
  {
    dprintf("No filename given for def-character (%s)\n",name);
    exit(0);
  }
  desc=lcdr(desc);  //  skip filename


  Cell *mrph=assoc(l_morph,desc);     // check for morph info
  morph_power=0;
  if (!NILP(mrph))
  {
    mrph=lcdr(mrph);
    morph_mask=cash.reg_object(fn,lcar(mrph),SPEC_IMAGE,1);
    morph_power=lnumber_value(lcar(lcdr(mrph)));
  } else morph_mask=-1;

  Cell *sa=assoc(l_state_art,desc);
  if (NILP(sa))
  {
    dprintf("missing state state art in def-character (%s)\n",name);
    exit(0);
  } 
  
  sa=lcdr(sa);   // list of state sequences
  while (!NILP(sa))
  {
    int num=lnumber_value(lcar(lcar(sa)));
    if (seq[num])    
      dprintf("Warning : state '%s' defined multiply for object %s\n"
	     "          using first definition\n",state_names[num],name);
    else    
      seq[lnumber_value(lcar(lcar(sa)))]=new sequence(fn,lcar(lcdr(lcar(sa))),lcar(lcdr(lcdr(lcar(sa)))));
    sa=lcdr(sa);    
  }

  Cell *range=assoc(l_range,desc);
  if (!NILP(range))
  {
    rangex=lnumber_value(lcar(lcdr(range)));
    rangey=lnumber_value(lcar(lcdr(lcdr(range)))); 
  } else 
  {
    rangex=100;
    rangey=50;
  }




  Cell *mf=assoc(l_fields,desc);
  if (!NILP(mf))
  {
    mf=lcdr(mf);
    total_fields=0;
    while (!NILP(mf))
    {
      char *name=lstring_value(lcar(lcar(mf)));
      int t=default_simple.total_vars(),find=-1;
      for (int i=0;find<0 && i<t;i++)
        if (!strcmp(default_simple.var_name(i),name))
	  find=i;
      if (find<0)
      {
	lprint(assoc(l_fields,desc));
	dprintf("fields : no such var name \"%s\"\n",name);
	dprintf("current possiblities are : \n");
	for (int i=0;i<t;i++) dprintf("\"%s\" ",default_simple.var_name(i));
	dprintf("\n");
	exit(0);
      } 
      char *new_name=lstring_value(lcar(lcdr(lcar(mf))));
      total_fields++;

      fields=(named_field **)jrealloc(fields,sizeof(named_field *)*total_fields,"named_fields");
      fields[total_fields-1]=new named_field(find,new_name);
      mf=lcdr(mf);
    }
  } else total_fields=0;


  Cell *lg=assoc(l_logo,desc);
  if (NILP(lg)) 
  {
    if (get_cflag(CFLAG_IS_WEAPON))
    {
      lprint(desc);
      lbreak("object must have a logo defined if it is a weapon\n"
	     "example '(logo . (""art/misc.spe"" . ""big waepon""))\n");
    }
    logo=-1;
  }
  else 
    logo=cash.reg_object(fn,lcdr(lg),SPEC_IMAGE,1);  
    */
}


sequence *character_type::get_sequence(character_state s)
{
  if (seq[s])
    return seq[s];
  else return seq[stopped];
}


character_type::~character_type()
{
  for (int i=0;i<ts;i++)  
    if (seq[i]) 
      delete seq[i];
  if (ts) jfree(seq);

  if (total_fields)
  {
    for (int i=0;i<total_fields;i++)
      delete fields[i];
    jfree(fields);
  }

  if (ts)
    jfree(seq_syms);
 
  if (tiv)
  {
    jfree(vars);
    jfree(var_index);
  }


}

extern long small_ptr_size(void *ptr);


void chk_sizes()
{
  for (int i=0;i<total_objects;i++)
  {
    figures[i]->check_sizes();
  }
}


void character_type::check_sizes()
{
  for (int i=0;i<ts;i++)
    if (seq[i] && small_ptr_size(seq[i])!=8)
      dprintf("bad size for state %d\n",i);
}




