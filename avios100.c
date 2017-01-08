/****************************************************************************
                            AVIOS version 1.0.0
        A VIrtual Operating System, Copyright (C) Neil Robertson 1997

                       Version date: 11th August 1997

 Created out of blood and sweat using incantations of the C after dusk and 
 in dark places found in London, England between January and August 1997. 

 Please read the README & COPYRIGHT files.

 Neil Robertson.

 Email    : neil@ogham.demon.co.uk
 Web pages: http://www.ogham.demon.co.uk/avios.html and neil.html

 ****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#ifdef _AIX
#include <sys/select.h>
#endif
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <time.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>

#include "avios100.h"

#define VERSION "1.0.0"

struct streams *get_stream();


/*** The main procedure which initialises the system then calls the
     mainloop function. ***/
main(argc,argv)
int argc;
char *argv[];
{
/* Go through command line args */
parse_command_line(argc,argv);

/* Startup */
printf("\n*** AVIOS version %s ***\n\nSystem booting...\n\n",VERSION);
if (daemon) {
	switch(fork()) {
		case -1: perror("ERROR: Unable to fork");  exit(-1);
		case 0:  break;
		default: exit(0);
		}
	/* Calling either ioctl(0,TIOCNOTTY,NULL) or setpgrp() *should* 
	   dissociate the process from its controlling tty (which is what I want 
	   to do with a daemon process) but the ioctl call only seems to work 
	   on Linux and the setpgrp doesn't work on anything. Ho hum. */
	}

if (init_file==NULL) init_file=INIT_FILE;
init_system(); 
create_system_variables();
parse_init_file();
sysboot=time(0);

mainloop();
}



/*** This function contains the Avios main loop which is basically a round 
     robin scheduler though the actual instruction count for swapout is done 
     by exec_process()  */
mainloop()
{
struct process *pcs_next;
time_t systime;
int ret;

systime=0;

/* The main loop itself */
while(1) {
	real_current_pcs=NULL;
	if (first_pcs->next==NULL) {
		uptime();
		write_syslog("*** System halted: all processes exited. ***");  
		exit(0);
		}
	if (time(0)>systime) systime=time(0); 

	/* Check for any connects to TCP ports */
	check_for_connects();

	/* Loop through processes */
	for(current_pcs=first_pcs->next;current_pcs!=NULL;current_pcs=pcs_next) {
		pcs_next=current_pcs->next;

		/* real_current_pcs used in sig_handler as current_pcs is 
		   corrupted by a number of the runtime functions */
		real_current_pcs=current_pcs; 

		/* Load in new process and run */
		load_process_state(current_pcs);

		/* See if process timer (if set) has reached its goal. */
		if (current_pcs->timer_exp &&
		    current_pcs->timer_exp<=(int)systime &&
		    current_pcs->t_int_proc!=NULL &&
		    current_pcs->int_enabled &&
		    !current_pcs->interrupted) {
			set_variable("$int_mesg",NULL,"0 TIMER",1);
			current_pcs->old_status=current_pcs->status;
			current_pcs->status=TIMER_INT;
			current_pcs->timer_exp=0;
			}
		inst_cnt=0;

		/* Switch on status */
		switch(current_pcs->status) {
			case IMAGE      :
			case CHILD_DWAIT:
			case SPEC_DWAIT  : continue;

			case OUTPUT_WAIT:
			if (current_outstream==NULL || 
			    !current_outstream->locked ||
			    current_outstream->owner->mesg_cnt<max_mesgs) {
				current_pcs->status=RUNNING;  break;
				}
			continue;

			case INPUT_WAIT: 
			read_stream();  
			/* If process has been reading off a socket and that 
			   has been closed remotely exit it */
			if (current_pcs->status==EXITING) {
				process_exit();  continue;
				}
			break;

			case SLEEPING:
			if ((int)systime>=current_pcs->sleep_to) {
				current_pcs->status=RUNNING;  
				current_pcs->sleep_to=0;  
				current_pcs->pc=prog_word[current_pcs->pc].end_pos+1;
				break;
				}
			continue;

			case CHILD_INT   :
			case NONCHILD_INT:
			case TIMER_INT   :
			if ((ret=interrupt_process())!=OK) {
				error_exit(ret,real_line);  continue;
				}
			if (current_pcs->status!=RUNNING) continue;
			break;
			
			case EXITING: process_exit();  continue;
			}

		/* Execute process for 'swapout_after' number of commands */
		ret=exec_process(current_pcs->pc,0);
		save_process_state(current_pcs,0);
		if (ret!=OK) {
			error_exit(ret,real_line);  continue;
			}
		}
	}
}



/*** Parse the command line arguments ***/
parse_command_line(argc,argv)
int argc;
char *argv[];
{
char *usage="Usage: %s [-i <init file>] [-s <syslog file>] [-d] [-v] [-h]\n";
int i;

/* These 3 vars are globals */
init_file=NULL;
syslog=NULL;
daemon=0;

/* Loop through args */
for(i=1;i<argc;++i) {
	if (!strcmp(argv[i],"-i")) {
		if (i==argc-1) {  fprintf(stderr,usage,argv[0]);  exit(-1);  }
		init_file=argv[i+1];  ++i;
		continue;
		}
	if (!strcmp(argv[i],"-s")) {
		if (i==argc-1) {  fprintf(stderr,usage,argv[0]);  exit(-1);  }
		syslog=argv[i+1];  ++i;
		continue;
		}
	if (!strcmp(argv[i],"-d")) {  daemon=1;  continue;  }
	if (!strcmp(argv[i],"-v")) {  printf("%s\n",VERSION);  exit(0);  }
	if (!strcmp(argv[i],"-h")) {
		printf("\nAVIOS version %s\nCopyright (C) Neil Robertson 1997\n\n",VERSION); 
		printf(usage,argv[0]);
		printf("\n-i: Set the initalisation file (default is 'init').\n-s: Set the system log file (default is standard output).\n");
		printf("-d: Run as a daemon.\n-v: Print version number.\n-h: Print this help message.\n\n");
		exit(0);
		}
	fprintf(stderr,usage,argv[0]);
	exit(-1);
	}
}


/*** Initialise system stuff and non process specific variables ***/
init_system()
{
setup_signals();
srand((int)time(0));

/* These are initialised to defaults in case they're not set in init file */
code_path[0]='\0';
root_path[0]='\0';
allow_ur_path=ALLOW_UR_PATH;
max_processes=MAX_PROCESSES;
max_mesgs=MAX_MESGS;
max_errors=MAX_ERRORS;
exit_remain=EXIT_REMAIN;
swapout_after=SWAPOUT_AFTER;
colour_def=COLOUR_DEF;
ignore_sigterm=IGNORE_SIGTERM;
kill_any=KILL_ANY;
child_die=CHILD_DIE;
wait_on_dint=WAIT_ON_DINT;
connect_timeout=0; /* 0 means wait for TCP timeout */

process_count=1;

first_pcs=NULL;
last_pcs=NULL;
current_pcs=NULL;
real_current_pcs=NULL;

first_port=NULL;
last_port=NULL;

first_stream=NULL;
last_stream=NULL;
}



/*** Create a dummy process that holds all the common system variables.
     The dummy process will *always* be first_pcs and we can refer to it
     in the code using that variable. ***/
create_system_variables()
{
int e,ret,flags;

if ((ret=create_process("system_dummy",1))!=OK) goto ERROR;
set_string(&first_pcs->site,"<null>");

/* Create process name array and process count */
flags=READONLY | ARRAY;
if ((ret=create_variable("$pcs",NULL,NULL,flags,1))!=OK) goto ERROR;
if ((ret=set_variable("$pcs:\"1\"",NULL,"system_dummy",1))!=OK) goto ERROR;
if ((ret=create_variable("$pcs_count",NULL,NULL,READONLY,1))!=OK) goto ERROR;
if ((ret=set_variable("$pcs_count",NULL,"1",1))!=OK) goto ERROR;

/* Avios version */
if ((ret=create_variable("$version",NULL,NULL,READONLY,1))!=OK) goto ERROR;
if ((ret=set_variable("$version",NULL,VERSION,1))!=OK) goto ERROR;

/* Max messages in queues - set after init file parsed */
if ((ret=create_variable("$max_mesgs",NULL,NULL,READONLY,1))!=OK) goto ERROR;

/* Create error array. Ignore "OK" 'error' as we can't have an array position
   zero anyway. */
if ((ret=create_variable("$error",NULL,NULL,flags,1))!=OK) goto ERROR;
for(e=1;e<NUM_ERRS;++e) {
	sprintf(text,"$error:\"%d\"",e);
	if ((ret=set_variable(text,NULL,error_mesg[e],1))!=OK) goto ERROR;
	}

save_process_state(first_pcs,1);
return;

ERROR:
fprintf(stderr,"ERROR: %s whilst creating system_dummy process.\n",error_mesg[ret]);  
exit(ret);
}



/*** Load and parse the init file ***/
parse_init_file()
{
FILE *fp;
int line_num,done_params;
char line[ARR_SIZE+1],word[ARR_SIZE+1];

if (!(fp=fopen(init_file,"r"))) {
	perror("ERROR: Unable to open init file");  exit(-1);
	}
printf("Parsing init file...\n");
line_num=0;
done_params=0;

/* Find a section header */
fgets(line,ARR_SIZE,fp);
while(!feof(fp)) {
	++line_num;
	word[0]='\0';
	sscanf(line,"%s",word);
	if (!strlen(word) || word[0]=='#') {
		fgets(line,ARR_SIZE,fp);  continue;
		}
	if (!strcmp(word,"PARAMETERS:")) {
		line_num=parse_param_section(fp,line_num);
		done_params=1;
		}
	else {
		if (!strcmp(word,"PROCESSES:")) {
			if (!done_params) {
				fprintf(stderr,"INIT ERROR: PARAMETERS section header must come before PROCESSES on line %d.\n",line_num);
				exit(-1);
				}
			parse_process_section(fp,line_num);  return;
			}
		else {
			fprintf(stderr,"INIT ERROR: Invalid section header on line %d.\n",line_num);
			exit(-1);
			}
		}
	fgets(line,ARR_SIZE,fp);
	}
fprintf(stderr,"INIT ERROR: PROCESSES section header missing.\n");
exit(-1);
}



/*** Parse the PARAMETERS section of the init file ***/
parse_param_section(fp,line_num)
FILE *fp;
int line_num;
{
int i,val,yesno;
char line[ARR_SIZE+1],w1[ARR_SIZE+1],w2[ARR_SIZE+1];

char *coms[]={
"code_path","root_path","colour_def","allow_ur_path","kill_any",
"child_die","ignore_sigterm","wait_on_dint","max_errors",
"max_mesgs","max_processes","exit_remain","swapout_after",
"connect_timeout"
};

fgets(line,ARR_SIZE,fp);
while(!feof(fp)) {
	++line_num;
	if (line[0]=='#') {
		fgets(line,ARR_SIZE,fp);  continue;
		}
	w1[0]='\0';  w2[0]='\0';
	sscanf(line,"%s %s",w1,w2);

	/* Newline means end of section so return */
	if (!strlen(w1)) {
		sprintf(text,"%d",max_mesgs);
		set_variable("$max_mesgs",NULL,text,1);
		return line_num;  
		}
	if (!strcmp(w1,"the_ode")) the_ode();

	if (!strcmp(w2,"NO")) yesno=0;
	else if (!strcmp(w2,"YES")) yesno=1;
		else yesno=-1;

	for(i=0;i<14;++i) {
		if (!strcmp(coms[i],w1)) {
			val=atoi(w2);

			/* This looks yucky, but it works so fuck style... */
			if (i>2 && i<8 && yesno==-1) goto ERROR;
			if (i>7 && !isinteger(w2,0)) goto ERROR;
 			if (i>8 && i<13 && val<1) goto ERROR;
			if (i==13 && val<0) goto ERROR;

			switch(i) {
				case 0: 
				strcpy(code_path,w2);  
				if (code_path[strlen(code_path)-1]!='/')
					strcat(code_path,"/");
				break;

				case 1: 
				strcpy(root_path,w2);  
				if (root_path[strlen(root_path)-1]!='/')
					strcat(root_path,"/");
				break;

				case 2:
				if (!strcmp(w2,"ON")) colour_def=1;
				else if (!strcmp(w2,"OFF")) colour_def=0;
					else goto ERROR;
				break;

				case 3: allow_ur_path=yesno;  break;
				case 4: kill_any=yesno;   break;
				case 5: child_die=yesno;  break;
				case 6: ignore_sigterm=yesno; break;
				case 7: wait_on_dint=yesno; break;

				case 8: max_errors=val;  break;
				case 9: max_mesgs=val;  break;
				case 10: max_processes=val;  break;
				case 11: exit_remain=val;  break;
				case 12: swapout_after=val;  break;
				case 13: connect_timeout=val;  break;
				}
			break;
			}
		}
	if (i==14) {
		fprintf(stderr,"INIT ERROR: Invalid option on line %d.\n",line_num);
		exit(-1);
		}
	fgets(line,ARR_SIZE,fp);
	}
ERROR:
fprintf(stderr,"INIT ERROR: Invalid option value on line %d.\n",line_num);
exit(-1);
}



the_ode()
{
char *data="!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!Uif!evtl!jt!hpof!boe!uif!nppo!jt!csjhiu-!b!mpof!usbwfmmfs!fodpnqbttft!uif!ojhiu/!Uispvhi!uif!wjsuvbm!tfb!if!sjeft-!ofwfs!lopxjoh!xifsf!uif!ofyu!mboegbmm!mjft/!Cvu!uifo!vqpo!uif!cpx-!b!Ebubtqsjuf!ibt!bqqfbsfe!up!ufmm!ijn!ipx-!up!gjoe!uif!tpmvujpo!up!uif!Hsfbu!Benjot!uftu/!If!nvtu!ifbe!gps!uif!ebsl!Bwjpt!Gpsftu/!(Nboz!xbzt!jo!dbo!cf!dpotusvdufe!cz!uiff-!boe!zpv!xjmm!tff!uibu!uif!tpmvujpo!jt!gsff(////////!!!!";
char *sptr,*cptr,*ptr;
int spaces,i;

signal(SIGINT,SIG_DFL);
signal(SIGTSTP,SIG_DFL);
spaces=1;
cptr=sptr=data;
printf("@=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-)\n");
while(1) {
	ptr=cptr;
	printf("\r |");
	for(i=0;i<70;++i) {
		if (*ptr=='\0') ptr=sptr;
		putchar(*ptr-1);  ++ptr;
		}
	putchar('|');
	fflush(stdout);
	if (*(++cptr)=='\0') cptr=sptr;
	if (spaces) { 
		if (*sptr=='!') ++sptr; else spaces=0;
		}
#ifndef NO_USLEEP
	usleep(250000);
#else
	sleep(1);
#endif
	}
}




/*** Parse the PROCESSES section of the init file ***/
parse_process_section(fp,line_num)
FILE *fp;
int line_num;
{
int i,ret,argc,option;
int port,new_pid,images,back;
char line[ARR_SIZE+1],*argv[MAX_ARGC+1],filename[100],pid[30];
char *s,*s2,c;

/* Loop through init file */
images=0;
fgets(line,ARR_SIZE,fp);
while(!feof(fp)) {
	++line_num;

	/* Get arguments from the input line. Its inefficient to create the 
	   argv array only to destroy it after its been used but what the 
	   hell, its neater that creating the system vars here. */
	argc=0;
	s=line;
	s2=NULL;
	option=0;

	/* Loop through chars on current init file line */
	while(1) {
		while(*s<33 && *s) ++s;
		if (!*s) {
			if (option && option!=3) {
				fprintf(stderr,"INIT ERROR: Syntax error on line %d.\n",line_num);
				exit(-1);
				}
			}
		s2=s;
		while(*s2>32 && *s2!='#') ++s2;
		if (*s2=='#') break;
		c=*s2;  *s2='\0'; 

		/* Check options */
		switch(option) {
			case 0:
			back=0;  port=0;
			if (strcmp(s,"TERM")) {
				if (!strcmp(s,"BACK")) back=1; 
				else if ((port=atoi(s))<1 || !isinteger(s,0)) {
						fprintf(stderr,"INIT ERROR: Invalid port on line %d.\n",line_num);
						exit(-1);
						}
				}
			if (daemon && !port && !back) {
				fprintf(stderr,"INIT ERROR: Cannot have terminal processes when running as a daemon on line %d.\n",line_num);
				exit(-1);
				}
			*s2=c;  s=s2;  option=1;
			continue;
	
			case 1:
			strcpy(filename,s);  
			*s2=c;  s=s2;  option=2;  
			continue;
			}
		option=3;

		/* Got to process name and number */
		if ((argv[argc]=(char *)malloc((int)(s2-s)+1))==NULL) {
			fprintf(stderr,"INIT ERROR: Memory allocation error on line %d.\n",line_num);
			exit(-1);
			}
		strcpy(argv[argc],s);
		*s2=c;	s=s2;  
		if (!*s || ++argc>MAX_ARGC) break;
		} /* end while */

	/* Rem'ed out line */
	if (!option) {
		fgets(line,ARR_SIZE,fp);  continue;
		}

	/* Got process info , now create it */
	if ((new_pid=get_new_pid())==-1) {
		fprintf(stderr,"INIT ERROR: Too many processes on line %d.\n",line_num);  
		exit(-1);
		}

	/* Set system process array entry. */
	load_process_state(first_pcs);
	sprintf(pid,"$pcs:\"%d\"",new_pid); 
	set_variable(pid,NULL,argv[0],1);
	save_process_state(first_pcs,0);

	/* Create a process list entry, create its port then load a program 
	   into its space. If process will be sitting on a port we create an
	   image which will be used to spawn copies on every connect, else if
	   process is running on the terminal we just run it straight away. */
	printf("\n=> Creating ");
	if (port) {  printf("image");  images++;  }
	else if (back) printf("background"); else printf("terminal");
 	printf(" process %d (%s)...\n",new_pid,argv[0]);

	if (create_process(argv[0],new_pid)!=OK) {
		fprintf(stderr,"INIT ERROR: Memory allocation error on line %d.\n",line_num);
		exit(-1);
		}
	if (port) {
		set_string(&current_pcs->site,"<null>");
		current_pcs->local_port=port;
		create_port_entry(current_pcs,port,line_num);
		}
	else {
		if (back) set_string(&current_pcs->site,"<BACK>");
		else set_string(&current_pcs->site,"<TERM>");
		current_pcs->status=RUNNING;
		}
	set_string(&current_pcs->filename,filename);

	/* Load the actual program */
	printf("   Loading program file '%s'...\n",filename);
	real_line=-1;
	if ((ret=process_setup(filename,argc,argv,1))!=OK) {
		if (real_line!=-1) {
			if (incf==NULL) incf=filename;
			fprintf(stderr,"ERROR: %s on line %d in file '%s' (Init line %d).\n",error_mesg[ret],real_line,incf,line_num);
			}
		else fprintf(stderr,"ERROR: %s for file %s (Init line %d).\n",error_mesg[ret],filename,line_num);
		exit(ret);
		}
	if (back) {
		/* Background processes not attached to terminal or socket */
		get_stream("STDIN")->external=-1;
		get_stream("STDOUT")->external=-1;
		}
	current_pcs->pc=current_proc->start_pos;
	save_process_state(current_pcs,1);

	/* Free temp arg vars */
	for(i=0;i<argc;++i) free(argv[i]);

	fgets(line,ARR_SIZE,fp);
	}
fclose(fp);
if (process_count==1) { /* 1 cos always have system dummy process */
	fprintf(stderr,"INIT ERROR: PROCESSES section is empty.\n");
	exit(-1);
	}
printf("\nSystem booted with Unix PID %d.\n\n",getpid());
sprintf(text,"*** All processes loaded: %d real, %d images. ***",process_count-images-1,images);
write_syslog(text);
}



/*****************************************************************************
                             SIGNAL FUNCTIONS
 *****************************************************************************/

/*** This will catch a signal , print message to the log and exit the 
     whole server ***/
void sig_handler(sig)
int sig;
{
struct process *pcs;
char line[100],*file;
int pcnt,icnt,ppid;

switch(sig) {
	case SIGTERM:
	if (ignore_sigterm) {
		write_syslog("SIGTERM (termination signal) received; ignoring...");  
		signal(SIGTERM,sig_handler);
		return;
		}
	write_syslog("SIGTERM (termination signal) received; exiting...");
	break;

	case SIGINT:
	write_syslog("SIGINT (^C at terminal) received; exiting...");
	break;

	case SIGTSTP:
	write_syslog("SIGTSTP (^Z at terminal) received; dumping status...");
	break;

	case SIGSEGV:
	write_syslog("PANIC!: SIGSEGV (segmentation fault) occured; exiting...");
	break;

	/* You'll never get a bus error with linux on a PC since the x86 
	   hardware does not generate the equivalent hardware interrupt */
	case SIGBUS:
	write_syslog("PANIC!: SIGBUS (bus error) occured; exiting...");
	}

/* Display processes loaded. Give process id, name, status and current line 
   number being executed. "-->" shows which process system was executing when
   interrupt occured. */
ppid=0;
pcnt=0;
icnt=0;
write_syslog("");
write_syslog("PROCESS STATUS DUMP:");
write_syslog("   PID NAME      PPID STATUS       FILE        LINE COMMAND");

/* This doesn't list the system_dummy process , no point in doing so */
for(pcs=first_pcs->next;pcs!=NULL;pcs=pcs->next) {
	if (pcs==real_current_pcs) sprintf(line,"-->%3d",pcs->pid);
	else sprintf(line,"   %3d",pcs->pid);

	if (pcs->parent!=NULL) ppid=pcs->parent->pid; else ppid=0;

	if (strlen(pcs->name)>10) {
		pcs->name[8]='.';  pcs->name[9]='.';  pcs->name[10]='\0';
		}
	file=pcs->prog_word[pcs->pc].filename;

	/* If filename too long to fit nicely on screen put dots at the end */
	if (strlen(file)>12) {
		file[10]='.';  file[11]='.';  file[12]='\0';
		}
	sprintf(text,"%s %-10s %3d %-12s %-12s %3d %s",line,pcs->name,ppid,status_name[pcs->status],pcs->prog_word[pcs->pc].filename,pcs->prog_word[pcs->pc].real_line,pcs->prog_word[pcs->pc].word);
	write_syslog(text);

	if (pcs->status==IMAGE) ++icnt;
	++pcnt;
	}
sprintf(text,"Total of %d processes (%d real, %d images).",pcnt,pcnt-icnt,icnt);
write_syslog(text);
write_syslog("");

/* If ^Z caught then pause else exit */
if (sig==SIGTSTP) {
	/* Can't pause if daemon 'cos can't use getchar */
	if (!daemon) {
		write_syslog("SYSTEM PAUSED. Press any key to continue...");
		getchar(); 
		}
	write_syslog("Continuing...");
	signal(SIGTSTP,sig_handler);
	return;
	}
uptime();
write_syslog("*** System halted: all processes killed. ***");
exit(-1);
}



/*** Set up signal catching ***/
setup_signals()
{
signal(SIGTERM,sig_handler);
signal(SIGSEGV,sig_handler);
signal(SIGBUS,sig_handler);
signal(SIGINT,sig_handler);
signal(SIGTSTP,sig_handler);

signal(SIGILL,SIG_IGN);
signal(SIGTRAP,SIG_IGN);
signal(SIGIOT,SIG_IGN);
signal(SIGCONT,SIG_IGN);
signal(SIGHUP,SIG_IGN);
signal(SIGQUIT,SIG_IGN);
signal(SIGABRT,SIG_IGN);
signal(SIGFPE,SIG_IGN);
signal(SIGPIPE,SIG_IGN);
signal(SIGTTIN,SIG_IGN);
signal(SIGTTOU,SIG_IGN);
}



/*****************************************************************************
                            PORT/SOCKET FUNCTIONS
 *****************************************************************************/

create_port_entry(pcs,port,line_num)
struct process *pcs;
int port,line_num;
{
struct ports *new,*old;

/* See if port is already reserved */
for(old=first_port;old!=NULL;old=old->next) 
	if (old->port==port) break;

/* Create new entry */
if ((new=(struct ports *)malloc(sizeof(struct ports)))==NULL) {
	fprintf(stderr,"INIT ERROR: Memory allocation error on line %d.\n",line_num);
	exit(-1);
	}
if (first_port==NULL) first_port=new;
else last_port->next=new;
last_port=new;
new->next=NULL;
new->port=port;
new->process=pcs;

/* Create the socket itself */
if (old==NULL) create_listen_socket(new,line_num);
else new->listen_sock=old->listen_sock;
}



/*** Create the listen socket ***/
create_listen_socket(new,line_num)
struct ports *new;
int line_num;
{
struct sockaddr_in bind_addr;
int size,on;

printf("   Creating socket on port %d...\n",new->port);

/* Create socket */
if  ((new->listen_sock=socket(AF_INET,SOCK_STREAM,0))==-1) {
	sprintf(text,"INIT ERROR: Socket creation error on line %d.",line_num);
	perror(text);  
	exit(-1);
	}

/* Allow ports to be re-used when on a TIME_WAIT */
on=1;
size=sizeof(struct sockaddr_in);
setsockopt(new->listen_sock,SOL_SOCKET,SO_REUSEADDR,(char *)&on,sizeof(on));

/* Bind socket to address */
bind_addr.sin_family=AF_INET;
bind_addr.sin_addr.s_addr=INADDR_ANY;
bind_addr.sin_port=htons(new->port);
if (bind(new->listen_sock,(struct sockaddr *)&bind_addr,size)==-1) {
	sprintf(text,"INIT ERROR: Socket bind() error on line %d.",line_num);
	perror(text);  
	exit(-1);
	}

/* Make it listen for connections */
if (listen(new->listen_sock,20)==-1) {
	sprintf(text,"INIT ERROR: Socket listen() error on line %d.",line_num);
	perror(text);  
	exit(-1);
	}
}



/*** Get net address of accepted connection ***/
char *get_ip_address(acc_addr)
struct sockaddr_in acc_addr;
{
static char site[100];
struct hostent *host;

strcpy(site,(char *)inet_ntoa(acc_addr.sin_addr)); /* get number addr */
if ((host=gethostbyaddr((char *)&acc_addr.sin_addr,4,AF_INET))!=NULL)
        strcpy(site,host->h_name); /* copy name addr. */
return site;
}



/*** Check to see if we have any connections on the TCP ports ***/
check_for_connects()
{
struct ports *pt,*pt2;
struct timeval tm;
struct sockaddr_in acc_addr;
fd_set mask;
int accept_sock,size,ret,done;
char port[6];

FD_ZERO(&mask);
for(pt=first_port;pt!=NULL;pt=pt->next)
	FD_SET(pt->listen_sock,&mask);

tm.tv_sec=0;
tm.tv_usec=0;
select(FD_SETSIZE,&mask,0,0,&tm);

/* Go through the list of listening ports and see if we have a new connect on
   any of them. If we do, spawn new copies of any processes attached to that 
   port and set their stdin & out to be the accept_sock */
for(pt=first_port;pt!=NULL;pt=pt->next) {
	/* See if we've checked same socket already */
	done=0;
	for(pt2=first_port;pt2!=pt;pt2=pt2->next) {
		if (pt2->listen_sock==pt->listen_sock) {  done=1;  break;  }
		}

	/* See if listen socket has been connected to */
	if (!done && FD_ISSET(pt->listen_sock,&mask)) {

		/* Accept connection */
		size=sizeof(struct sockaddr_in);
		if ((accept_sock=accept(pt->listen_sock,(struct sockaddr *)&acc_addr,&size))==-1) {
			sprintf(text,"ERROR: Socket accept() error on port %d for process %s: %s",pt2->port,pt2->process->name,sys_errlist[errno]);
			write_syslog(text);
			continue;
			}

		/* Set socket to non-blocking */
		if ((fcntl(accept_sock,F_SETFL,O_NONBLOCK))==-1) {
			sprintf(text,"ERROR: Socket fcntl() error on port %d for process %s: %s",pt2->port,pt2->process->name,sys_errlist[errno]);
			write_syslog(text);
			close(accept_sock);
			continue;
			}

		/* Spawn all processes sharing same port */
		for(pt2=pt;pt2!=NULL;pt2=pt2->next) {
			if (pt2->listen_sock!=pt->listen_sock) continue;
			if ((ret=spawn_process(pt2->process,0,0))!=OK) {
				sprintf(text,"ERROR: Unable to spawn process %s: %s.",pt2->process->name,error_mesg[ret]); 
				write_syslog(text);
				close(accept_sock);
				continue;
				}
			load_process_state(last_pcs);

			/* Set up some process vars */
			current_pcs->status=RUNNING;
			current_pcs->pc=pt2->process->pc;
			current_pcs->remote_port=(int)ntohs(acc_addr.sin_port);
			free(current_pcs->site);
			current_pcs->site=NULL;
			set_string(&current_pcs->site,get_ip_address(acc_addr));
			set_variable("$site",NULL,current_pcs->site,1);
			sprintf(port,"%d",current_pcs->remote_port);
			set_variable("$rport",NULL,port,1);

			/* If get_stream returns NULL here the whole thing is 
			   fucked anyway so it won't matter if we crash with 
			   a SIGSEGV */
			get_stream("STDIN")->external=accept_sock;
			get_stream("STDOUT")->external=accept_sock;

			sprintf(text,"Connection on %d from %s:%s; spawned process %d (%s).",pt->port,current_pcs->site,port,current_pcs->pid,current_pcs->name);
			write_syslog(text);
			}
		} /* end if */
	}
}
		
		


/*****************************************************************************
                         PROCESS CONTROL FUNCTIONS
 *****************************************************************************/

/*** Add a process entry to the process list ***/
create_process(name,pid)
char *name;
int pid;
{
struct process *pcs;

if ((pcs=(struct process *)malloc(sizeof(struct process)))==NULL) 
	return ERR_MALLOC;

/* Update process count */
if (pid!=1) {
	process_count++;
	load_process_state(first_pcs);
	sprintf(text,"%d",process_count);
	set_variable("$pcs_count",NULL,text,1);
	}

if (first_pcs==NULL) {
	first_pcs=pcs;  pcs->prev=NULL;
	}
else {
	last_pcs->next=pcs;  pcs->prev=last_pcs;
	}
pcs->next=NULL;
last_pcs=pcs;
current_pcs=pcs;

/* Set process name & stuff */
pcs->name=NULL;
set_string(&pcs->name,name);
pcs->filename=NULL;
set_string(&pcs->filename,"<null>");
pcs->site=NULL;
pcs->pid=pid;
pcs->status=IMAGE;
pcs->old_status=IMAGE;
pcs->pc=0;
pcs->over=0;
pcs->local_port=0;
pcs->remote_port=0;
pcs->eof=0;
pcs->sleep_to=0;
pcs->exit_code=OK;
pcs->parent=NULL;
pcs->input_buff=NULL;
pcs->mesg_q=NULL;
pcs->mesg_cnt=0;
pcs->sleep_to=0;
pcs->timer_exp=0;
pcs->death_pid=0;
pcs->death_mesg=NULL;
pcs->wait_pid=0;
pcs->wait_var=NULL;
pcs->wait_var_proc=NULL;
pcs->c_int_proc=NULL; /* child interrupt proc */
pcs->n_int_proc=NULL; /* nonchild */
pcs->t_int_proc=NULL; /* timer */
pcs->int_enabled=1;
pcs->interrupted=0;
pcs->colour=colour_def;
pcs->err_cnt=0;
pcs->exit_cnt=0;
pcs->input_vnum=1;

/* Initialise structure pointers */
pcs->prog_word=NULL;

pcs->rstack_start=NULL;
pcs->rstack_ptr=NULL;

pcs->pstack_start=NULL;
pcs->pstack_ptr=NULL;

pcs->lstack_start=NULL;
pcs->lstack_ptr=NULL;

pcs->first_proc=NULL;
pcs->last_proc=NULL;
pcs->current_proc=NULL;

pcs->first_var=NULL;
pcs->last_var=NULL;
pcs->current_var=NULL;

pcs->first_loop=NULL;
pcs->last_loop=NULL;
pcs->current_loop=NULL;

pcs->first_label=NULL;
pcs->last_label=NULL;

pcs->current_instream=NULL;
pcs->current_outstream=NULL;

pcs->created=time(0);

return OK;
}



/*** Remove a process entry from the process list ***/
destroy_process(pcs)
struct process *pcs;
{
struct process *pcs2;
struct array *arr,*arrprev;
char pid[10];

sprintf(pid,"%d",pcs->pid);

/* Reset any parent flags of other processes */
for(pcs2=first_pcs;pcs2!=NULL;pcs2=pcs2->next) {
	if (pcs2->parent==pcs) pcs2->parent=pcs->parent;
	}

/* Destroy process */
if (pcs==first_pcs) {
	first_pcs=pcs->next;
	if (first_pcs==NULL) last_pcs=NULL;
	else first_pcs->prev=NULL;
	}
else {
	pcs->prev->next=pcs->next;
	if (pcs==last_pcs) last_pcs=pcs->prev;
	else pcs->next->prev=pcs->prev;
	}
free(pcs);


/* Remove process $pcs entry */
load_process_state(first_pcs);

get_variable("$pcs",NULL); /* Set current_var. $pcs guaraunteed to exist */
arrprev=NULL;
for(arr=current_var->arr;arr!=NULL;arr=arr->next) {
	if (!strcmp(arr->subscript,pid)) {
		if (arrprev==NULL) current_var->arr=arr->next;
		else arrprev->next=arr->next;
		free(arr->subscript);
		FREE(arr->value);
		free(arr);
		break;
		}
	arrprev=arr;
	}
/* Set new process count */
process_count--;
load_process_state(first_pcs);
sprintf(text,"%d",process_count);
return set_variable("$pcs_count",NULL,text,1);
}



/*** Recreate a new process in the image of the parent. This function looks
     horrid to read and it was horrid to write but theres no simpler way
     that I can think of to do this. And yes I know it doesn't check every
     call for malloc errors, if the system runs out of memory then it'll
     probably crash elsewhere anyway :) ***/
spawn_process(par,child,preserve)
struct process *par;
int child,preserve;
{
struct process *cur,*new;
struct procedures *proc,*newproc,*oldproc;
struct vars *var,*newvar,*oldvar;
struct array *arr,*arrnext;
struct loops_choose *loop,*newloop,*oldloop;
struct labels *label,*newlabel,*oldlabel;
struct streams *st,*stnext;
struct proc_stack *ps;
struct result_stack *rs;
struct loop_stack *ls;
int ret,w,w2,ok,size,new_pid;
char pid[10];

if ((new_pid=get_new_pid())==-1) return ERR_MAX_PROCESSES;
if (preserve) {
	cur=current_pcs;  save_process_state(cur,0);
	}

/* Create new process (which also initialises all its pointers) */
if ((ret=create_process(par->name,new_pid))!=OK) {
	if (preserve) load_process_state(cur);   
	return ret;
	}
new=current_pcs;

/* Do the simple bit copying. Some of these should not need to be copied since
   the should not be set if the process is spawning (eg sleep_to, wait_var) but
   just in case I alter the code in the future so they could be set somehow.. */
set_string(&new->filename,par->filename);
set_string(&new->site,par->site);
new->pid=new_pid;
new->num_words=par->num_words;
new->status=par->status;
new->old_status=par->old_status;
new->over=0;
new->local_port=par->local_port;
new->remote_port=par->remote_port;
new->eof=par->eof;
new->pc=par->prog_word[par->pc].end_pos+1;
new->int_enabled=par->int_enabled;
new->interrupted=par->interrupted;
new->sleep_to=par->sleep_to;
new->wait_pid=par->wait_pid;
new->mesg_cnt=par->mesg_cnt;
new->timer_exp=par->timer_exp;
if (par->mesg_q!=NULL) set_string(&new->mesg_q,par->mesg_q);
if (par->input_buff!=NULL) set_string(&new->input_buff,par->input_buff);
if (par->wait_var!=NULL) set_string(&new->wait_var,par->wait_var);
if (child) new->parent=par;

/* Now the hard parts, start with the program text */
size=sizeof(struct program)*par->num_words+1;
if (!(new->prog_word=(struct program *)malloc(size))) {
	ret=ERR_MALLOC;  goto ERROR;
	}
memcpy(new->prog_word,par->prog_word,size);
for(w=0;w<par->num_words;++w) {
	if (!(new->prog_word[w].word=(char *)malloc(strlen(par->prog_word[w].word)+1))) {
		
		ret=ERR_MALLOC;  goto ERROR;
		}
	new->prog_word[w].word=NULL;
	set_string(&new->prog_word[w].word,par->prog_word[w].word);
	new->prog_word[w].filename=NULL;
	set_string(&new->prog_word[w].filename,par->prog_word[w].filename);
	}


/* .. then copy the procedures */
oldproc=NULL;
newproc=NULL;
for(proc=par->first_proc;proc!=NULL;proc=proc->next) {
	if (!(newproc=(struct procedures *)malloc(sizeof(struct procedures)))) {
		ret=ERR_MALLOC;  goto ERROR;
		}
	if (oldproc!=NULL) oldproc->next=newproc;
	else new->first_proc=newproc;

	newproc->name=NULL;
	set_string(&newproc->name,proc->name);
	newproc->start_pos=proc->start_pos;
	newproc->ei_di_int=proc->ei_di_int;
	newproc->old_int=proc->old_int;
	newproc->next=NULL;

	proc->copy=newproc;
	oldproc=newproc;
	}
new->current_proc=par->current_proc->copy;
new->last_proc=newproc;
if (par->wait_var_proc!=NULL) new->wait_var_proc=par->wait_var_proc->copy;
if (par->c_int_proc!=NULL) new->c_int_proc=par->c_int_proc->copy;
if (par->n_int_proc!=NULL) new->n_int_proc=par->n_int_proc->copy;
if (par->t_int_proc!=NULL) new->t_int_proc=par->t_int_proc->copy;


/* Now the variables... */
ok=0;
oldvar=NULL;
newvar=NULL;
for(var=par->first_var;var!=NULL;var=var->next) {
	ok=1;
	if (!(newvar=(struct vars *)malloc(sizeof(struct vars)))) {
		ret=ERR_MALLOC;  goto ERROR;
		}
	newvar->prev=oldvar;
	if (oldvar!=NULL) oldvar->next=newvar;
	else new->first_var=newvar;

	newvar->name=NULL;
	set_string(&newvar->name,var->name);

	newvar->value=NULL;
	set_string(&newvar->value,var->value);

	newvar->flags=var->flags;
	newvar->arr=NULL;
	copy_array(var,newvar);

	newvar->proc=var->proc;
	if (var->proc!=NULL) newvar->proc=var->proc->copy;
	newvar->next=NULL;

	var->copy=newvar;
	oldvar=newvar;
	}	
if (ok) {
	/* Set any reference variables. var->copy will refer to the just
	   created twin variable in the new process. */
	for(var=par->first_var;var!=NULL;var=var->next) {
		if (var->refvar!=NULL) var->copy->refvar=var->refvar->copy;
		else var->copy->refvar=NULL;
		}
	}
new->last_var=newvar;
first_var=new->first_var;
last_var=new->last_var;

sprintf(pid,"%d",new->pid);
if ((ret=set_variable("$pid",NULL,pid,1))!=OK) goto ERROR;
if (child) sprintf(pid,"%d",par->pid); else strcpy(pid,"0");
if ((ret=set_variable("$ppid",NULL,pid,1))!=OK) goto ERROR;

/*.. and now the loops .. */
ok=0;
oldloop=NULL;
newloop=NULL;
for(loop=par->first_loop;loop!=NULL;loop=loop->next) {
	ok=1;
	if (!(newloop=(struct loops_choose *)malloc(sizeof(struct loops_choose)))) {
		ret=ERR_MALLOC;  goto ERROR;
		}
	if (oldloop!=NULL) oldloop->next=newloop;
	else new->first_loop=newloop;

	memcpy(newloop,loop,sizeof(struct loops_choose));
	newloop->proc=loop->proc->copy;
	if (loop->foreach_var!=NULL) 
		newloop->foreach_var=loop->foreach_var->copy;
	newloop->next=NULL;

	loop->copy=newloop;
	oldloop=newloop;
	}
if (ok) {
	if (par->current_loop!=NULL) new->current_loop=par->current_loop->copy;
	else new->current_loop=NULL;
	}
new->last_loop=newloop;


/* .. and the labels, *yawn* this is getting tedious */
ok=0;
oldlabel=NULL;
newlabel=NULL;
for(label=par->first_label;label!=NULL;label=label->next) {
	if (!(newlabel=(struct labels *)malloc(sizeof(struct labels)))) {
		ret=ERR_MALLOC;  goto ERROR;
		}
	if (oldlabel!=NULL) oldlabel->next=newlabel;
	else new->first_label=newlabel;

	newlabel->name=NULL;
	set_string(&newlabel->name,label->name);
	newlabel->pos=label->pos;
	if (label->proc!=NULL) newlabel->proc=label->proc->copy;
	newlabel->next=NULL;

	oldlabel=newlabel;
	}
new->last_label=newlabel;


/* Copy streams (except for specific pid stream) */
sprintf(pid,"%d",new_pid);
for(st=first_stream;st!=NULL;st=st->next) {
	if (st->owner==par && strcmp(st->name,pid)) {
		if ((ret=create_stream(st->name,st->filename,st->external,st->internal,st->rw,new))!=OK) 
			goto ERROR;
		}
	/* Newly created stream is last on list */
	if (st==par->current_instream) new->current_instream=last_stream;
	if (st==par->current_outstream) new->current_outstream=last_stream;
	}	
if ((ret=create_stream(pid,NULL,-1,MESG_Q,READWRITE,new))!=OK) goto ERROR;

/* Copy proc stack */
pstack_start=NULL;
pstack_ptr=NULL;
for(ps=par->pstack_start;ps!=NULL;ps=ps->next) {
	push_pstack(ps->proc->copy);  
	pstack_ptr->ret_pc=ps->ret_pc;
	}
new->pstack_start=pstack_start;
new->pstack_ptr=pstack_ptr;

/* Copy loop stack */
lstack_start=NULL;
lstack_ptr=NULL;
for(ls=par->lstack_start;ls!=NULL;ls=ls->next) push_lstack(ls->loop->copy);
new->lstack_start=lstack_start;
new->lstack_ptr=lstack_ptr;

/* Copy result stack even though it should always be empty as spawn can't
   be nested. */
rstack_start=NULL;
rstack_ptr=NULL;
for(rs=par->rstack_start;rs!=NULL;rs=rs->next) push_rstack(rs->value);
new->rstack_start=rstack_start;
new->rstack_ptr=rstack_ptr;

/* Set system process array entry */
load_process_state(first_pcs);
sprintf(pid,"$pcs:\"%d\"",new->pid); 
if ((ret=set_variable(pid,NULL,new->name,1))!=OK) goto ERROR;
save_process_state(first_pcs,0);
	
if (preserve) load_process_state(cur);
return OK;


/* Here I have to free everything thats been reserved. Yuk. Pity there isn't
   some C facility to free all memory malloced within the current function */
ERROR:
/* Delete some process attributes */
FREE(new->name);
FREE(new->filename);
FREE(new->mesg_q);
FREE(new->input_buff);
FREE(new->wait_var);
FREE(new->site);

/* Delete program text */
if (new->prog_word) {
	for(w2=0;w2<w;++w2) free(new->prog_word[w2].word);
	free(new->prog_word);
	}

/* Delete procedures */
for(proc=new->first_proc;proc!=NULL;proc=newproc) {
	newproc=proc->next;  free(proc->name);  free(proc);
	}

/* Delete variables and any array data they hold */
for(var=new->first_var;var!=NULL;var=newvar) {
	newvar=var->next;
	free(var->name);
	FREE(var->value);
	for(arr=var->arr;arr!=NULL;arr=arrnext) {
		arrnext=arr->next;
		free(arr->subscript);
		FREE(arr->value);
		free(arr);
		}
	free(var);
	}

/* Delete loops */
for(loop=new->first_loop;loop!=NULL;loop=newloop) {
	newloop=loop->next;  free(loop);
	}

/* Delete labels */
for(label=new->first_label;label!=NULL;label=newlabel) {
	newlabel=label->next;  free(label->name);  free(label);
	}

/* Delete streams owned by process. Don't need to check if any other
   process is referencing them as they won't have had a chance to. */
for(st=first_stream;st!=NULL;st=stnext) {
	stnext=st->next; 
	if (st->owner==new) destroy_stream(st);
	}

/* Delete stacks */
pstack_start=new->pstack_start; 
while(pstack_start) pop_pstack();

lstack_start=new->lstack_start; 
while(lstack_start) pop_lstack();

rstack_start=new->rstack_start; 
while(rstack_start) pop_rstack();

/* Remove process from linked list and reload calling process if appropriate */
destroy_process(new);
if (preserve) load_process_state(cur);  
return ret;
}



/*** Save process data & pointers. If we are running we only need to save the 
     ones that change but if we have just loaded a program save them all ***/
save_process_state(pcs,all)
struct process *pcs;
int all;
{
if (pcs==NULL) return;

pcs->rstack_start=rstack_start;
pcs->rstack_ptr=rstack_ptr;

pcs->pstack_start=pstack_start;
pcs->pstack_ptr=pstack_ptr;

pcs->lstack_start=lstack_start;
pcs->lstack_ptr=lstack_ptr;

pcs->first_var=first_var;
pcs->last_var=last_var;

pcs->current_proc=current_proc;
pcs->current_loop=current_loop;
pcs->current_instream=current_instream;
pcs->current_outstream=current_outstream;

if (all) {
	pcs->prog_word=prog_word;
	pcs->num_words=num_words;

	pcs->first_proc=first_proc;
	pcs->last_proc=last_proc;

	pcs->first_loop=first_loop;
	pcs->last_loop=last_loop;

	pcs->first_label=first_label;
	pcs->last_label=last_label;
	}

return OK;
}



/*** Load process data & pointers ***/
load_process_state(pcs)
struct process *pcs;
{
if (pcs==NULL) return;

prog_word=pcs->prog_word;
num_words=pcs->num_words;

rstack_start=pcs->rstack_start;
rstack_ptr=pcs->rstack_ptr;

pstack_start=pcs->pstack_start;
pstack_ptr=pcs->pstack_ptr;

lstack_start=pcs->lstack_start;
lstack_ptr=pcs->lstack_ptr;

first_proc=pcs->first_proc;
last_proc=pcs->last_proc;
current_proc=pcs->current_proc;

first_var=pcs->first_var;
last_var=pcs->last_var;
current_var=pcs->current_var;

first_loop=pcs->first_loop;
last_loop=pcs->last_loop;
current_loop=pcs->current_loop;

first_label=pcs->first_label;
last_label=pcs->last_label;

current_instream=pcs->current_instream;
current_outstream=pcs->current_outstream;

current_pcs=pcs;
return OK;
}



/*** Interrupt the current process ***/
interrupt_process()
{
struct procedures *proc;
int status;

status=current_pcs->status;
current_pcs->status=RUNNING;
switch(status) {
	case CHILD_INT:
	/* If no interrupt procedure to jump to do nothing */
	if (current_pcs->c_int_proc==NULL) {
		current_pcs->status=current_pcs->old_status;  return OK;
		}
	proc=current_pcs->c_int_proc;
	break;

	case NONCHILD_INT:
	if (current_pcs->n_int_proc==NULL) {
		current_pcs->status=current_pcs->old_status;  return OK;
		}
	proc=current_pcs->n_int_proc;
	break;

	case TIMER_INT:
	if (current_pcs->t_int_proc==NULL) {
		current_pcs->status=current_pcs->old_status;  return OK;
		}
	proc=current_pcs->t_int_proc;
	}

/* Set up pstack data and pc */
pstack_ptr->ret_pc=current_pcs->pc;
current_pcs->pc=prog_word[proc->start_pos].end_pos+1;
current_proc=proc;
current_pcs->interrupted=1;

/* This needs to be done (even though from a cursary glance it seems 
   completely pointless) to avoid a problem which is too boring to 
   explain here */
if (proc->ei_di_int) {
	proc->old_int=current_pcs->int_enabled;
	current_pcs->int_enabled=proc->ei_di_int-1;
	sprintf(text,"%d",current_pcs->int_enabled);
	set_variable("$int_enabled",NULL,text,1);
	}

return push_pstack(proc);
}



/*** Get a process by its pid ***/
struct process *get_process(pid)
int pid;
{
struct process *pcs;

for(pcs=first_pcs;pcs!=NULL;pcs=pcs->next) 
	if (pcs->pid==pid) return pcs;
return NULL;
}



/*** Free all allocated memory used by the current process before we exit ***/
process_exit()
{
struct process *parent,*pcs,*pcs2,*ipcs;
struct procedures *pnext;
struct loops_choose *lnext;
struct labels *label,*labnext;
struct vars *vnext;
struct array *arr,*arrnext;
struct streams *st,*stnext;
char pid[10];
int w,int_type;
int hours,mins,secs;

pcs=current_pcs;
if (pcs->exit_cnt++<exit_remain) return;

/* See if we're to interrupt another process on death. */
if (pcs->death_pid && (ipcs=get_process(pcs->death_pid))!=NULL) {
	/* See if we can interrupt it. */
	if (!ipcs->int_enabled ||
	    ipcs->interrupted ||
	    ipcs->status==CHILD_INT || 
	    ipcs->status==NONCHILD_INT ||
	    ipcs->status==TIMER_INT ||
	    ipcs->status==IMAGE) {
		/* No we can't at the moment */
		if (wait_on_dint) return; /* MUST interrupt before we die */
		else goto SKIP;
		}

	/* See if process to interrupt is a parent of deceased process */
	if (pcs->parent==ipcs) int_type=CHILD_INT; else int_type=NONCHILD_INT;

	/* Set other processes vars */
	save_process_state(pcs,0);

	load_process_state(ipcs);
	sprintf(text,"%d %s",pcs->pid,pcs->death_mesg);
	set_variable("$int_mesg",NULL,text,1);
	ipcs->old_status=ipcs->status;
	ipcs->status=int_type;
	save_process_state(ipcs,0);

	load_process_state(pcs);
	}

SKIP:
FREE(pcs->death_mesg);

/* If exit_code < 0 then exit was initiated by "exit" command within the 
   process else it was done by the system */
if (current_pcs->exit_code<0)
	sprintf(text,"Process %d (%s) exited with process code %d.",pcs->pid,pcs->name,-current_pcs->exit_code-1);
else sprintf(text,"Process %d (%s) exited with system code %d.",pcs->pid,pcs->name,current_pcs->exit_code);

/* Give process runtime */
secs=(int)(time(0) - pcs->created);
hours=secs/3600;  secs%=3600;
mins=secs/60;  secs%=60;
sprintf(text,"%s Runtime: %02d:%02d:%02d",text,hours,mins,secs);
write_syslog(text);

/* Free some stuff */
free(pcs->name);
free(pcs->filename);
FREE(pcs->mesg_q);
FREE(pcs->site);
FREE(pcs->input_buff);
FREE(pcs->wait_var);

/* Delete program text */
if (prog_word==NULL) return;
for(w=0;w<num_words;++w) {
	free(prog_word[w].word);  free(prog_word[w].filename);
	}
free(prog_word);

/* Delete procedure list */
for(current_proc=first_proc;current_proc!=NULL;current_proc=pnext) {
	pnext=current_proc->next;  
	FREE(current_proc->name);  
	free(current_proc);
	}

/* Delete loops */
for(current_loop=first_loop;current_loop!=NULL;current_loop=lnext) {
	lnext=current_loop->next;  free(current_loop);
	}

/* Delete labels */
for(label=first_label;label!=NULL;label=labnext) {
	labnext=label->next;  FREE(label->name);  free(label);
	}

/* Delete vars */
for(current_var=first_var;current_var!=NULL;current_var=vnext) {
	vnext=current_var->next;
	FREE(current_var->name);
	FREE(current_var->value);

	if ((arr=current_var->arr)!=NULL) {
		for(;arr!=NULL;arr=arrnext) {
			arrnext=arr->next;
			FREE(arr->subscript);
			FREE(arr->value);
			free(arr);
			}
		}
	free(current_var);
	}

/* Delete stacks */
while(pstack_start) pop_pstack();
while(lstack_start) pop_lstack();
while(rstack_start) pop_rstack();

/* Delete streams owned by process */
for(st=first_stream;st!=NULL;st=stnext) {
	stnext=st->next;
	if (st->owner==pcs) {
		/* See if another process is referencing it (eg if a mesg q). 
		   Another processes instream should never bet set to one of 
		   our streams but check anyway just in case. */
		for(pcs2=first_pcs;pcs2!=NULL;pcs2=pcs2->next) {
			if (pcs2->current_outstream==st) 
				pcs2->current_outstream=NULL;
			if (pcs2->current_instream==st) 
				pcs2->current_instream=NULL;
			}
		destroy_stream(st);
		}
	}

parent=pcs->parent;
sprintf(pid,"%d",pcs->pid);

/* If parent process is on CHILD_DWAIT notify it of death */
if (parent!=NULL && parent->status==CHILD_DWAIT) {
	load_process_state(parent);
	current_pcs->status=RUNNING; 
	set_variable(parent->wait_var,parent->wait_var_proc,pid,1);
	free(parent->wait_var); 
	parent->wait_var=NULL;
	parent->pc+=2; /* Advance parents counter */
	save_process_state(parent,0);
	}

/* See if any process is waiting for us to die and also if we're to kill any
   child processes upon exiting so that too. */
for(pcs2=first_pcs;pcs2!=NULL;pcs2=pcs2->next) {
	if (pcs2->status==SPEC_DWAIT && pcs2->wait_pid==pcs->pid) {
		pcs2->pc=pcs2->prog_word[pcs2->pc].end_pos+1;
		pcs2->status=RUNNING;  
		}
	/* This will cause cascade effect as child processes killed here
	   run this function too */
	if (child_die && pcs2->parent==pcs) {
		sprintf(text,"Child process %d (%s) being terminated.",pcs2->pid,pcs2->name);
		write_syslog(text);
		pcs2->status=EXITING;  
		pcs2->exit_code=OK;
		}
	}

/* Remove process from list */ 
destroy_process(pcs);
}



/*** Get a process id for a new process. This may seem a bit overkill and 
     you're probably wondering why I don't just add 1 to the last pid. Well
     what happens when the number finally wraps round? OK it'll take a 
     long long time but this keeps the pid values low anyway as any old
     ones will be reused. ***/
get_new_pid()
{
struct process *pcs;
int max,pid;

/* Find a free process id. Start from 2 cos system_dummy (pid 1) is always
   present */
for(pid=2;pid<=max_processes+1;++pid) 
	if ((pcs=get_process(pid))==NULL) return pid;

/* Find current maximum and make new_pid one beyond that. Max processes
   doesn't count the system dummy */
max=1;
for(pcs=first_pcs;pcs!=NULL;pcs=pcs->next) if (pcs->pid>max) max=pcs->pid;
if (max<=max_processes) return max+1; 

/* Reached max processes */
return -1;
}




/*****************************************************************************
                            STREAM FUNCTIONS

 These perform all the necessary actions to control the systems internal and
 external streams.
 ****************************************************************************/

/*** Get a stream based on its name. If name is just a number then we
     are looking for a process message queue. ***/
struct streams *get_stream(name)
char *name;
{
struct streams *st;
int is_q;

if (name==NULL) return NULL;

/* Convert dummy message queue name into real message queue name */
if (!strcmp(name,"MESG_Q") && current_pcs) {
	sprintf(name,"%d",current_pcs->pid);  is_q=1;
	}
else is_q=isinteger(name,0);

for(st=first_stream;st!=NULL;st=st->next) {
	/* If looking for message queue ignore owner */
	if ((is_q || st->owner==current_pcs) && !strcmp(st->name,name)) 
		return st;
	}
return NULL;
}



/*** Create a stream that either points to an external file pointer or an
     internal memory area. ***/
create_stream(name,file,ex,in,rw,owner)
char *name,*file;
int ex,in,rw;
struct process *owner;
{
struct streams *st;

if ((st=get_stream(name))!=NULL) return OK;

if ((st=(struct streams *)malloc(sizeof(struct streams)))==NULL)
	return ERR_MALLOC;

if (first_stream==NULL) {
	first_stream=st;  st->prev=NULL;
	}
else {
	last_stream->next=st;  st->prev=last_stream;
	}
last_stream=st;

st->next=NULL;
st->external=ex;
st->internal=in;
st->rw=rw;
st->block=1;
st->con_sock=0;
st->echo=1;
st->locked=0;
st->owner=owner;
st->filename=NULL;
if (file!=NULL) set_string(&st->filename,file);
st->name=NULL;
return set_string(&st->name,name);
}



/*** Destroy a stream entry. No return value as there's no error it can 
     give that we need to know about */
destroy_stream(st)
struct streams *st;
{
struct streams *st2;

if (st==first_stream) {
	first_stream=st->next;
	if (first_stream==NULL) last_stream=NULL;
	else first_stream->prev=NULL;
	}
else {
	st->prev->next=st->next;
	if (st==last_stream) last_stream=st->prev;
	else st->next->prev=st->prev;
	}
if (current_instream==st) current_instream=NULL;
if (current_outstream==st) current_outstream=NULL;

/* If external file descriptor is a file or socket then close it but only
   if we don't have another process still using it (ie the process spawned or
   was spawned as a child/orphan and is hence sharing the _unix_ descriptor) */
if (st->external>2) {
	for(st2=first_stream;st2!=NULL;st2=st2->next) 
		if (st2->external==st->external) goto END;
	close(st->external);
	}
END:
free(st->name);
FREE(st->filename);
free(st);
}



/** Set the current input or output stream ***/
set_iostreams(out,name)
int out;
char *name;
{
struct streams *st;
int ret;

if ((st=get_stream(name))==NULL) return ERR_INVALID_STREAM; 

if (out) {
	/* Can't write to a stream unless WRITE, READWRITE or APPEND */
	if (st->rw==READ) return ERR_INVALID_STREAM_DIR;

	if ((ret=set_variable("$out",NULL,name,1))!=OK) return ret;
	current_outstream=st;
	}
else {
	/* Can't read from a stream if no the owner */
	if (st->owner!=current_pcs) return ERR_INVALID_STREAM;

	/* Can't read from a stream unless READ or READWRITE */
	if (st->rw!=READ && st->rw!=READWRITE) return ERR_INVALID_STREAM_DIR;

	if ((ret=set_variable("$in",NULL,name,1))!=OK) return ret;
	current_instream=st;
	}
return OK;
}



/*** Write to the current output stream ***/
write_stream(str)
char *str;
{
struct process *owner,*old_pcs;
char cnt[10],tmp[20],*str2;
int ret;

if (current_outstream==NULL) return ERR_INVALID_STREAM;

if (current_outstream->external!=-1) {

	/* Replace colour commands with escape codes so user see's the colour 
	   unless we're writing to a file or a process created connect socket 
	   (ie as long as we're writing to STDOUT) */
	if (current_outstream->filename==NULL && !current_outstream->con_sock) {
		if ((ret=replace_colour_coms(str,&str2))!=OK) return ret;
		/* EINTR means operation interrupted by a signal */
		do {
			ret=write(current_outstream->external,str2,strlen(str2));
		   } while(ret==-1 && errno==EINTR); 
		}
	else {
		do {
			ret=write(current_outstream->external,str,strlen(str));
		   } while(ret==-1 && errno==EINTR);
		}

	if (ret==-1) {
		if (!current_outstream->con_sock &&
		    is_sock_stream(current_outstream)) {
			/* Socket is controlling port socket which has closed 
			   remotely. Theres nothing we can do about it so kill 
			   process */
			current_pcs->status=EXITING;  return OK;
			}
		return ERR_WRITE_FAILED;
		}
	return OK;
	}

/* Internal streams. If string is nothing more than a single newline
   then don't bother sending it. There is only 1 type of internal stream
   at the moment which is MESG_Q  */
if (current_outstream->internal!=MESG_Q) return ERR_UNSET_STREAM;
if (strlen(str)==1 && *str=='\n') return OK;

/* Write our pid and name onto other processes queue as id first then write 
   message and end marker. Make sure we don't have 255 in the message string
   first though. */
str2=str;
while(*str2) if ((unsigned char)*(str2++)==255) return ERR_ILLEGAL_CHAR;

owner=current_outstream->owner;
sprintf(tmp,"%d\n",current_pcs->pid);
append_string(&(owner->mesg_q),tmp);
append_string(&(owner->mesg_q),str);
sprintf(tmp,"%c",255);
append_string(&(owner->mesg_q),tmp);

/* This is an ugly fudge but it works. Don't need to save owner state
   as no var pointers will be changed, only a var value. */
old_pcs=current_pcs;
save_process_state(old_pcs,0);

load_process_state(owner);
owner->mesg_cnt++;
sprintf(cnt,"%d",owner->mesg_cnt);
if ((ret=set_variable("$mesg_cnt",NULL,cnt,1))!=OK) return ret;

load_process_state(old_pcs);
return OK;
}



/*** Go through string an replace colour commands with ansi escape codes ***/
replace_colour_coms(str,str2)
char *str,**str2;
{
char *s,*s2;
int com,got,ncnt,len,col;

s=str;
if (col=current_pcs->colour) {
	/* Double the size of the input string will be enough though we need a 
   	   minimum size so reset codes will fit in so count num of newlines. */
	ncnt=0;
	while(*s) if (*s++=='\n') ++ncnt;
	len=(strlen(str)+1)*2+ncnt*5+5; /* +5 so have minimum size */
	}
else len=strlen(str)+1; /* No colour codes so same length or less */
if ((*str2=(char *)malloc(len))==NULL) return ERR_MALLOC;

/* Go through string */
s=str;
s2=*str2;
while(*s) {
	if (*s=='\n') {
		if (col) {
			memcpy(s2,"\033[0m\n",5); /* newline reset */
			s2+=5;
			}
		else *s2++='\n';
		++s; 
		continue;
		}
	if (*s=='/' && *(s+1)=='~') {  ++s;  continue;  }
	if (*s=='~') {
		if (s>str && *(s-1)=='/') {
			*s2++='~';  ++s;  continue;
			}
		/* Ok , we've perhaps got a colour command */
		++s;  got=0; 
		for(com=0;com<NUM_COLS;++com) {
			if (!strncmp(s,colcom[com],2)) {
				if (col) {
					memcpy(s2,colcode[com],strlen(colcode[com]));
					s2+=strlen(colcode[com]);
					}
				s+=2;  got=1;
				break;
				}
			}
		if (got) continue;
		*s2++='~';
		}
	*s2++=*s++;
	}
if (col) memcpy(s2,"\033[0m\0",5); /* end of line reset */
else *s2='\0';
return OK;
}



/*** Read from the current input stream and put data in current processes 
     input buffer. ***/
read_stream()
{
int rlen,ex,pos,gotit,readit,ret,is_sock;
struct timeval tm;
fd_set mask;
unsigned char c[1],*mesg,*tmp;
char cnt[10];

if (current_instream==NULL) return ERR_INVALID_STREAM;
current_pcs->eof=0;

/* External streams */
if (current_instream->external!=-1) {
	ex=current_instream->external;
	is_sock=is_sock_stream(current_instream);

	/* Get input from a non-blocking select (done by setting timeval
	   to zero) */
	*c='\0';
	pos=0;  
	rlen=0; 
	gotit=0;
	tm.tv_sec=0;
	tm.tv_usec=0;
	FD_ZERO(&mask);
	FD_SET(ex,&mask);
	select(FD_SETSIZE,&mask,0,0,&tm);

	/* See if we got anything. We read data into text buffer because 
	   calling append_string for each character read would be horribly 
	   slow */
	if (FD_ISSET(ex,&mask)) {
		gotit=1;  readit=0;

		/* EINTR means read returned due to a signal interrupt */
		while((rlen=read(ex,c,1))>0 || (rlen==-1 && errno==EINTR)) {
			readit=1;
			if (*c=='\r') continue;
			if (*c=='\n') break;
			text[pos]=*c;
			if (++pos==ARR_SIZE-1) {
				text[ARR_SIZE-1]='\0';
				append_string(&current_pcs->input_buff,text);
				pos=0;
				}
			}
		text[pos]='\0';

		/* If 1st char is 255 then its a telopt control code; ignore */
		if ((unsigned char)text[0]==255 && is_sock) return OK;

		/* Its a controlling port socket which has been closed so 
		   kill process */
		if (!readit && !current_instream->con_sock && is_sock) {
			current_pcs->status=EXITING;  return OK;
			}
		append_string(&current_pcs->input_buff,text);
		}

	/* If we've hit end of the line unsuspend process. */
	if (*c=='\n') current_pcs->status=RUNNING;

	/* If select said there was something to read and read returned 0 or 
	   -1 then we've hit the end of the file or theres no data */
	if (gotit && rlen<1) {
		/* If its socket which might be talking to a char mode
		   client see if we've really finished. */
		if (*c && is_sock) return set_variable("$eof",NULL,"0",1);
		current_pcs->eof=1;

		/* unsuspend process whether we've hit a newline or not */
		current_pcs->status=RUNNING; 
		return set_variable("$eof",NULL,"1",1);
		}
	return set_variable("$eof",NULL,"0",1);
	}

/* Internal stream. Only the one type (MESG_Q) at the moment */
if (current_instream->internal!=MESG_Q) return ERR_UNSET_STREAM;

/* Read data off message q onto input buffer. This is an inefficient
   method but it allows compatability with external input using the
   input buffer at the same time. */
if ((mesg=(unsigned char *)current_pcs->mesg_q)==NULL) return OK; 
current_pcs->status=RUNNING;  /* data on queue,unsuspend process */

/* Go along queue and read one line into input buffer. We should never
   hit a '\0' before a '\n' because of the way write_stream works but
   just to be sure check in while() anyway */
pos=0;
while(*mesg && *mesg!='\n' && *mesg!=255) {
	text[pos]=*mesg;
	if (++pos==ARR_SIZE) {
		text[ARR_SIZE-1]='\0';
		append_string(&current_pcs->input_buff,text);
		pos=0;
		}
	++mesg;
	}
text[pos]='\0';
append_string(&current_pcs->input_buff,text);

/* Have we reached end of an individual message? */
if (*mesg==255) {
	current_pcs->mesg_cnt--; /* end marker */
	sprintf(cnt,"%d",current_pcs->mesg_cnt);
	if ((ret=set_variable("$mesg_cnt",NULL,cnt,1))!=OK) return ret;
	}

/* Have we reached end of the queue itself? */
if (!*mesg || !*(mesg+1)) {
	current_pcs->eof=1;  
	free(current_pcs->mesg_q);
	current_pcs->mesg_q=NULL;
	return set_variable("$eof",NULL,"1",1);
	}

/* Still data on queue , shift start of data down. */
++mesg; /* To skip terminating '\n' */
if ((tmp=(unsigned char *)malloc(strlen(mesg)+1))==NULL) 
	return ERR_MALLOC;
strcpy(tmp,mesg);  
free(current_pcs->mesg_q);
current_pcs->mesg_q=(char *)tmp;

return set_variable("$eof",NULL,"0",1);
}



/*** Seek along the current input stream ***/
seek_stream(com_num,whence,offset)
int com_num,whence,offset;
{
unsigned char *mesg,*m;
char cnt[10],c[1];
int lines,rlen,ex,mcnt;

if (current_instream==NULL) return ERR_INVALID_STREAM;

/* External stream */
if ((ex=current_instream->external)!=-1) {
	if (com_num==CSEEK) {
		if (lseek(ex,offset,whence)==-1) return push_rstack("0");
		return push_rstack("1");
		}

	/* LSEEK. Return to start of file if SEEK_SET */
	if (whence==SEEK_SET && lseek(ex,0,SEEK_SET)==-1)
		return push_rstack("0"); /* Can't return to start */
	if (!offset) return push_rstack("1");

	/* Count lines */	
	lines=0;
	while((rlen=read(ex,c,1))>0 || (rlen==-1 && errno==EINTR)) {
		if (c[0]=='\n') ++lines;
		if (lines==offset) return push_rstack("1");
		}
	return push_rstack("0");
	}

if (current_instream->internal!=MESG_Q) return ERR_INVALID_STREAM;

/* Whence is ignored here because the current position will always be
   the start. Can't seek backwards or on an empty queue or off the end
   of the queue. */
if (!offset) return push_rstack("1");
mesg=(unsigned char *)current_pcs->mesg_q;

if (com_num==LSEEK) {
	if (offset<0 || mesg==NULL) return push_rstack("0");

	/* Go through lines */
	lines=0;  mcnt=0;
	while(*mesg && lines<offset) {
		if (*mesg=='\n') lines++;
		else if (*mesg==255) {  lines++;  mcnt++;  }
		mesg++;
		}
	if (lines!=offset || !*(mesg+1)) return push_rstack("0");
	if (*(mesg+1)==255) {  mcnt++;  mesg++;  }

	/* Set queue */
	if ((m=(unsigned char *)malloc(strlen(mesg)+1))==NULL)
		return ERR_MALLOC;
	strcpy(m,mesg);
	free(current_pcs->mesg_q);
	current_pcs->mesg_q=(char *)m;
	current_pcs->mesg_cnt-=mcnt;
	sprintf(cnt,"%d",current_pcs->mesg_cnt);
	set_variable("$mesg_cnt",NULL,cnt,1);
	return push_rstack("1");
	}

/* CSEEK */
if (offset<0 || mesg==NULL || offset>=strlen(mesg)) return push_rstack("0");

offset+=(*(mesg+offset)=='\n'); /* Don't want to start next mesg on a \n */
if ((mesg=(unsigned char *)malloc(strlen(mesg)+1-offset))==NULL)
	return ERR_MALLOC;
strcpy(mesg,current_pcs->mesg_q+offset);

/* See how messages we've skipped */
m=(unsigned char *)current_pcs->mesg_q;
while(offset--) {
	if (*m==255) {
		current_pcs->mesg_cnt--; /* end marker */
		sprintf(cnt,"%d",current_pcs->mesg_cnt);
		set_variable("$mesg_cnt",NULL,cnt,1);
		}
	}
free(current_pcs->mesg_q);

/* Have we reached end of the queue itself? */
if (!*mesg || !*(mesg+1)) {
	current_pcs->eof=1;  
	current_pcs->mesg_q=NULL;
	set_variable("$eof",NULL,"1",1);
	return push_rstack("1");
	}
current_pcs->mesg_q=(char *)mesg;
return push_rstack("1");
}



/*** See if stream is a socket stream or not ***/
is_sock_stream(st)
struct streams *st;
{
if (st==NULL) return 0;
if (st->external>2 && st->filename==NULL) return 1;
return 0;
}


/*****************************************************************************
                             MISCELLANIOUS FUNCTIONS
            Miscellanious code which doesn't really belong anywhere else.
 *****************************************************************************/


/*** Print uptime in system log for when system has stopped ***/
uptime()
{
int days,hours,mins,secs;

secs=(int)(time(0)-sysboot);

days=secs/86400;  secs%=86400;
hours=secs/3600;  secs%=3600;
mins=secs/60;     secs%=60;

sprintf(text,"Uptime: %d days, %d hours, %d mins, %d secs.",days,hours,mins,secs);
write_syslog(text);
}



/*** Return a string containing the time and date ***/
char *timestr()
{
static char str[20];
struct tm *tms; 
time_t tm_num;

/* Set up the structure */
time(&tm_num);
tms=localtime(&tm_num);
sprintf(str,"%02d/%02d, %02d:%02d:%02d",tms->tm_mday,tms->tm_mon+1,tms->tm_hour,tms->tm_min,tms->tm_sec);
return str;
}



/*** Write to the system log (or to stdout if not specified on command
     line) ***/
write_syslog(str)
char *str;
{
FILE *fp;

if (syslog==NULL) {
	printf("%s: %s\n",timestr(),str);  return;
	}
if (!(fp=fopen(syslog,"a"))) return; /* Can't do much about it */
fprintf(fp,"%s: %s\n",timestr(),str);
fclose(fp);
}



/*** Return 1 if a string is NULL or consists only of '\0'. This saves
     doing this comparison everywhere ***/
isnull(str)
char *str;
{
return (str==NULL || *str=='\0');
}



/*** See if string is an integer, if neg set then allows negative numbers ***/
isinteger(str,neg)
char *str;
int neg;
{
char *s;

if ((s=str)==NULL) return 0;
while(*s) {
	if (neg && s==str && *s=='-') ++s;
	if (!isdigit(*s)) return 0;
	++s;
	}
if (s==str) return 0;
return 1;
}



/*** See if string is alphanumeric ***/
isalnumstr(str)
char *str;
{
char *s;

if ((s=str)==NULL) return 0;
while(*s) {
	if (!isalnum(*s) && *s!='_' && *s!='.') return 0;
	++s;
	}
if (s==str) return 0;
return 1;
}



/*** Get the number of the command ***/
get_command_number(word)
char *word;
{
int i;

for(i=0;i<NUM_COMS;++i)
	if (!strcmp(commands[i],word)) return i;
return -1;
}


/*** Give a string a value. The return value of this function is mostly not
     checked simply because its called so often. The code would become a 
     complete mess if I checked it every time I used it. If we do run out of
     memory we'll soon know about it anyway. ***/
set_string(strptr,value)
char **strptr,*value;
{
/* If we can fit new string into old don't free mem */
if (*strptr!=NULL && value!=NULL && strlen(value)>strlen(*strptr)) {
	free(*strptr);  *strptr=NULL;
	}
if (value==NULL) {
	FREE(*strptr);  *strptr=NULL;  return OK;
	}
if (*strptr==NULL && (*strptr=(char *)malloc(strlen(value)+1))==NULL) 
     return ERR_MALLOC;
strcpy(*strptr,value);
return OK;
}



/*** Append a value onto a string. The return value of this is currently
     never checked. ***/
append_string(strptr,value)
char **strptr,*value;
{
char *newstr;
int slen;

if (value==NULL) return OK; /* append nothing */

if (*strptr==NULL) slen=0; else slen=strlen(*strptr);
if ((newstr=(char *)malloc(slen+strlen(value)+1))==NULL) return ERR_MALLOC;

*newstr='\0';
if (*strptr!=NULL) {
	strcpy(newstr,*strptr);  free(*strptr);
	}
strcat(newstr,value);
*strptr=newstr;
return OK;
}



/*** Compare 2 strings ***/
compare_strings(str1,str2)
char *str1,*str2;
{
if (str1==NULL && str2==NULL) return 0;
else {
	if (str1==NULL) return (!strlen(str2) ? 0 : -1);
	else if (str2==NULL) return (!strlen(str1) ? 0 : 1);
	}
return strcoll(str1,str2);
}



/*** Exit function for when theres an error with a process ***/
error_exit(err,line)
int err,line;
{
sprintf(text,"ERROR: Process %d (%s): ",current_pcs->pid,current_pcs->name);

if (real_line==-1) sprintf(text,"%s%s.",text,error_mesg[err]);
else sprintf(text,"%s%s in file '%s' on line %d.",text,error_mesg[err],prog_word[current_pcs->pc].filename,line);
write_syslog(text);

if (!max_errors || current_pcs->err_cnt<max_errors) return;

if (max_errors>1) {
	sprintf(text,"Process %d (%s) reached maximum error count, terminating...",current_pcs->pid,current_pcs->name);
	write_syslog(text);
	}
current_pcs->status=EXITING;
current_pcs->exit_code=err;
process_exit();
}



/*** Create a file pathname using the root_path as appropriate ***/
char *create_path(path)
char *path;
{
static char fullpath[200];

if (*path=='/') {
	/* If allowing full unix root path don't append our root path. */
	if (allow_ur_path) {
		strcpy(fullpath,path);  return fullpath;
		}
	/* Don't include slash at start of path */
	sprintf(fullpath,"%s%s",root_path,path+1);
	return fullpath;
	}
sprintf(fullpath,"%s%s",root_path,path);
return fullpath;
}
	
	


/*****************************************************************************

                    Process load and parse functions

 *****************************************************************************/

/*** Call the load and parse routines. ***/
process_setup(filename,argc,argv,boot)
int argc,boot;
char *filename,*argv[];
{
struct procedures *proc;
int ret;

incf=NULL;
if ((ret=init_process_globals())!=OK) return ret;
if ((ret=load_and_first_parse(filename,boot))!=OK) return ret;
if ((ret=second_parse())!=OK) return ret;
if ((ret=third_parse())!=OK) return ret;
if ((ret=create_process_variables())!=OK) return ret;

/* Set up globals */
if ((ret=exec_process(0,1))!=OK) return ret;

/* Find program start */
for(proc=first_proc;proc!=NULL;proc=proc->next) {
	if (!strcmp(proc->name,"main")) {
		current_proc=proc;
		if ((ret=push_pstack(proc))!=OK) return ret;
		return setup_arg_vars(argc,argv);
		}
	}
real_line=-1;
return ERR_MAIN_PROC_MISSING;
}



/*** Initialise process variables ***/
init_process_globals()
{
memory_reserve=MEMORY_RESERVE;
if ((prog_word=(struct program *)malloc((memory_reserve+1)*sizeof(struct program)))==NULL) 
	return ERR_MALLOC;
num_words=0;

rstack_start=NULL;
rstack_ptr=NULL;
pstack_start=NULL;
pstack_ptr=NULL;
lstack_start=NULL;
lstack_ptr=NULL;

first_proc=NULL;
last_proc=NULL;
current_proc=NULL;

first_var=NULL;
last_var=NULL;
current_var=NULL;

first_loop=NULL;
last_loop=NULL;
current_loop=NULL;

first_label=NULL;
last_label=NULL;

current_instream=NULL;
current_outstream=NULL;

return OK;
}



/*** Load program words into prog_words structure and sort out operators. If 
     no program file is given load from stdin ***/
load_and_first_parse(filename,boot)
char *filename;
int boot;
{
FILE *fp,*main_fp;
char c,pathname[ARR_SIZE];
char *opstr="[]<>!="; /* The order is VITAL! */
int prog_line,got_word,brackets,ip,ret,main_real_line;
int word,tpos,in_string,in_rem,whitespace;
static char incfile[ARR_SIZE];

main_fp=NULL;
real_line=1;
prog_line=1;
got_word=0;
word=0;
tpos=0;
in_string=0;
in_rem=0;
brackets=0;
whitespace=1;
incfile[0]='\0';

sprintf(pathname,"%s%s",code_path,filename);
if (!(fp=fopen(pathname,"r"))) {
	real_line=-1;  return ERR_CANT_OPEN_FILE;
	}
c=getc(fp);
if (feof(fp)) {  fclose(fp);  return ERR_UNEXPECTED_EOF;  }

/* Go through file */
while(1) {
	/* If we've overun reserved mem , realloc an increased amount */
	if (word>=memory_reserve) {
		memory_reserve+=MEMORY_RESERVE;
		if ((prog_word=(struct program *)realloc(prog_word,(memory_reserve+1)*sizeof(struct program)))==NULL) {
			ret=ERR_MALLOC;  goto ERROR;
			}
		}

	/* Deal with include files */
	if (c=='&' && whitespace && !in_rem && !in_string) {
		if (main_fp!=NULL) {  ret=ERR_INCLUDE_LIMIT;  goto ERROR;  }

		/* get filename */
		ip=0;  
		c=getc(fp);
		while(!feof(fp) && c>32 && c!=';') {  
			incfile[ip]=c;  ip++;  c=getc(fp);  
			}
		if (!ip) {  fclose(fp);  return ERR_SYNTAX;  }
		incfile[ip]='\0';

		main_fp=fp;
		if (boot) printf("   > Loading include file '%s'...\n",incfile);
		sprintf(pathname,"%s%s",code_path,incfile);	
		if (!(fp=fopen(pathname,"r"))) {
			fclose(fp);  return ERR_CANT_OPEN_FILE;
			}
		if (c=='\n') real_line++;
		main_real_line=real_line;
		real_line=1;
		c=getc(fp);
		continue;
		}

	/* Deal with rem statements */
	if (!in_rem && whitespace && c=='#' && !in_string) {  
		/* Fool it into thinking its the end of the line */
		in_rem=1;  c='\n';  real_line--;
		}
	else {
		if (in_rem) {
			if (c=='\n') {  in_rem=0;  real_line++;  }
			goto CONT;
			}
		}

	/* Deal with char */
	switch(c) {
		case '\n': 
		if (in_string) {  ret=ERR_UNTERMINATED_STRING;  goto ERROR;  }
		if (brackets)  {  ret=ERR_MISSING_BRACKET;  goto ERROR;  }

		case ';':
		case ' ':
		case TAB:
		/* End of word/line */
		if (in_string) break;
		whitespace=1;
		if (tpos) {
			text[tpos]='\0';
			prog_word[word].word=NULL;
			set_string(&prog_word[word].word,text);
			prog_word[word].filename=NULL;
			if (main_fp) set_string(&prog_word[word].filename,incfile);
			else set_string(&prog_word[word].filename,filename);
			prog_word[word].real_line=real_line;
			prog_word[word].prog_line=prog_line;
			prog_word[word].op_num=-1;
			word++;
			}
		if (got_word && (c=='\n' || c==';')) {
			prog_line++;  got_word=0;
			}
		if (c=='\n') real_line++;
		tpos=0;
		goto CONT;

		case '[': brackets++;
		case ']': if (c==']') brackets--; /* need if in case of fall through */
		case '<':
		case '>':
		case '!':
		case '=':
		/* Single character operators */
		if (in_string) break;
		whitespace=0;
		if (tpos) {
			/* Save word up to current operator */
			text[tpos]='\0';
			prog_word[word].word=NULL;
			set_string(&prog_word[word].word,text);
			prog_word[word].filename=NULL;
			if (main_fp) set_string(&prog_word[word].filename,incfile);
			else set_string(&prog_word[word].filename,filename);
			prog_word[word].real_line=real_line;
			prog_word[word].prog_line=prog_line;
			prog_word[word].op_num=-1;
			word++;
			}
		else {
			/* Check for ,'<=','>=' and '!=' operators */
			if (word && c=='=') {
				switch(prog_word[word-1].word[0]) {
					case '!':
					set_string(&prog_word[word-1].word,"!=");
					prog_word[word-1].op_num=NOT_EQUAL;
					tpos=0;  goto CONT;

					case '<':
					set_string(&prog_word[word-1].word,"<=");
					prog_word[word-1].op_num=LESS_THAN_EQUALS;
					tpos=0;  goto CONT;

					case '>':
					set_string(&prog_word[word-1].word,">=");
					prog_word[word-1].op_num=GREATER_THAN_EQUALS;
					tpos=0;  goto CONT;
					}
				}
			}
		sprintf(text,"%c",c);
		prog_word[word].word=NULL;
		set_string(&prog_word[word].word,text);
		prog_word[word].filename=NULL;
		if (main_fp) set_string(&prog_word[word].filename,incfile);
		else set_string(&prog_word[word].filename,filename);
		prog_word[word].real_line=real_line;
		prog_word[word].prog_line=prog_line;
		prog_word[word].op_num=(int)strchr(opstr,c)-(int)opstr;
		word++;  tpos=0;
		goto CONT;

		case  '"': 
		/* Check if we're trying to print a quote using \" in string */
		if (in_string && tpos && text[tpos-1]=='\\') break;	
		in_string=!in_string;  
		break;

		default: whitespace=0;
		}
	text[tpos]=c;
	if (++tpos>ARR_SIZE) {  ret=ERR_LINE_TOO_LONG;  goto ERROR;  }
		
	CONT:
	got_word=1;
	c=getc(fp);
	if (feof(fp)) {
		if (main_fp==NULL) break;
		fclose(fp);  fp=main_fp;  main_fp=NULL;  
		real_line=main_real_line;
		incfile[0]='\0';
		c=getc(fp);
		if (feof(fp)) break;
		}
	}
if (in_string || in_rem) {  ret=ERR_UNEXPECTED_EOF;  goto ERROR;  }
fclose(fp);

/* Terminate array with dummy entry */
prog_word[word].word=NULL;
prog_word[word].filename=NULL;
prog_word[word].com_num=-1;
prog_word[word].op_num=-1;
prog_word[word].real_line=0;
prog_word[word].prog_line=0;

num_words=word;
return OK;

ERROR:
fclose(fp);
if (main_fp) fclose(main_fp); 
if (incfile[0]) incf=incfile;
return ret;
}



/*** This sets up the command variables in prog_words, makes sure commands
     are inside a procedure and checks the bracket count ***/
second_parse()
{
int i,j,brackets1,brackets2,in_proc;
int prog_line,com_num,prev_com_num;
char *w;

brackets1=0;
prog_line=0;
in_proc=0;
prev_com_num=-1;

/* go through word list and pick out commands */
for(i=0;i<num_words;++i) {
	w=prog_word[i].word;
	real_line=prog_word[i].real_line;
	incf=prog_word[i].filename;

	if (w[0]=='[') {
		brackets1++;
		if (i && prog_word[i-1].word[0]=='[') return ERR_SYNTAX; 
		}
	if (w[0]==']') brackets1--;
	if ((com_num=prog_word[i].com_num=get_command_number(w))==-1) continue;
	prog_line=prog_word[i].prog_line;

	/* If we've got here then we're dealing with a command */
	if (i) prev_com_num=prog_word[i-1].com_num;
	if (prev_com_num!=-1 &&
	    prev_com_num!=ALIAS &&
	    prog_line==prog_word[i-1].prog_line) return ERR_SYNTAX;

	switch(com_num) {
		case PROC   : in_proc=1;  break;
		case ENDPROC: in_proc=0;  break;
		case ALIAS:
		case VAR  :
		case SET  :  
		case SHARE:
		case UNSHARE: break;

		default : 
		if (!in_proc && prev_com_num!=ALIAS) 
			return ERR_UNEXPECTED_COMMAND;
		}

	/* find where command ends */
	brackets2=1;
	for(j=i+1;j<num_words;++j) {
		w=prog_word[j].word;
		if (w[0]=='[') brackets2++;
		if (w[0]==']') brackets2--;
		
		/* See if terminating bracket found */
		if (brackets1 && !brackets2 && w[0]==']') {
			prog_word[i].end_pos=j-1;  break;
			}
		if (prog_word[j].prog_line!=prog_line) {
			prog_word[i].end_pos=j-1;  break;
			}
		}
	prog_word[i].end_pos=j-1;
	}
if (in_proc) return ERR_UNEXPECTED_EOF;
return OK;
}



/*** Set up aliases, procs, loops, switches and label lists ***/
third_parse()
{
int pc,com_num,ret;

for(pc=0;pc<num_words;++pc) {
	com_num=prog_word[pc].com_num;
	real_line=prog_word[pc].real_line;
	incf=prog_word[pc].filename;

	switch(com_num) {
		case ALIAS  : 
		if ((ret=set_aliases(pc))!=OK) return ret;
		continue;

		case WHILE  : 
		case FOR    :
		case FOREACH:
		case DO     :
		case CHOOSE : 
		if ((ret=create_loop_entry(com_num,pc))!=OK) return ret;
		continue;

		case LABEL  : 
		if ((ret=create_label_entry(pc))!=OK) return ret;
		continue;

		case PROC  : 
		if (current_proc) return ERR_UNEXPECTED_COMMAND;
		if ((ret=create_procedure_entry(pc))!=OK) return ret;
		continue;

		case ENDPROC: 
		if (current_proc==NULL) return ERR_UNEXPECTED_COMMAND;
		current_proc=NULL;
		}
	}
return OK;
}



/*** Set up all the internal process $xxx interpreter variables and also set 
     up some default stream stuff. ***/
create_process_variables()
{
struct procedures *proc;
int ret,flags;
char pid[10],port[6];

/* Create procedure return value array and result var. $result is set after
   every procedure call but $proc stores the last returned values of each
   individual procedure for later reference so you don't have to check $result
   after each procedure call */
flags=READONLY | ARRAY;
if ((ret=create_variable("$proc",NULL,NULL,flags,1))!=OK) return ret;
for(proc=first_proc;proc!=NULL;proc=proc->next) {
	sprintf(text,"$proc:\"%s\"",proc->name); 
	if ((ret=set_variable(text,NULL,NULL,1))!=OK) return ret;
	}
if ((ret=create_variable("$result",NULL,NULL,READONLY,1))!=OK) return ret;

/* Process name */
if ((ret=create_variable("$name",NULL,NULL,READONLY,1))!=OK) return ret;
if ((ret=set_variable("$name",NULL,current_pcs->name,1))!=OK) return ret;

/* Process filename */
if ((ret=create_variable("$filename",NULL,NULL,READONLY,1))!=OK) return ret;
if ((ret=set_variable("$filename",NULL,current_pcs->filename,1))!=OK) return ret;

/* Parent process id */
if (current_pcs->parent!=NULL) sprintf(pid,"%d",current_pcs->parent->pid);
else strcpy(pid,"0");
if ((ret=create_variable("$ppid",NULL,NULL,READONLY,1))!=OK) return ret;
if ((ret=set_variable("$ppid",NULL,pid,1))!=OK) return ret;

/* Process id */
sprintf(pid,"%d",current_pcs->pid);
if ((ret=create_variable("$pid",NULL,NULL,READONLY,1))!=OK) return ret;
if ((ret=set_variable("$pid",NULL,pid,1))!=OK) return ret;

/* No. of messages on message queue */
if ((ret=create_variable("$mesg_cnt",NULL,NULL,READONLY,1))!=OK) return ret;
if ((ret=set_variable("$mesg_cnt",NULL,"0",1))!=OK) return ret;

/* Interrupt vars */
if ((ret=create_variable("$int_mesg",NULL,NULL,READONLY,1))!=OK) return ret;
if ((ret=create_variable("$int_enabled",NULL,NULL,READONLY,1))!=OK) return ret;
if ((ret=set_variable("$int_enabled",NULL,"1",1))!=OK) return ret; 

/* Last error trapped by "trap" command */
if ((ret=create_variable("$last_error",NULL,NULL,READONLY,1))!=OK) return ret;
if ((ret=set_variable("$last_error",NULL,"0",1))!=OK) return ret; 

/* End of file flag */
if ((ret=create_variable("$eof",NULL,NULL,READONLY,1))!=OK) return ret;
if ((ret=set_variable("$eof",NULL,"0",1))!=OK) return ret;

/* Current input and output streams */
if ((ret=create_variable("$in",NULL,NULL,READONLY,1))!=OK) return ret;
if ((ret=create_variable("$out",NULL,NULL,READONLY,1))!=OK) return ret;

/* This is set to 0 if a print command failed on a non-blocking stream else
   is set to 1 */
if ((ret=create_variable("$print_ok",NULL,NULL,READONLY,1))!=OK) return ret;
if ((ret=set_variable("$print_ok",NULL,"0",1))!=OK) return ret;

/* Network vars */
if ((ret=create_variable("$site",NULL,NULL,READONLY,1))!=OK) return ret;
if ((ret=set_variable("$site",NULL,current_pcs->site,1))!=OK) return ret;
sprintf(port,"%d",current_pcs->local_port);
if ((ret=create_variable("$lport",NULL,NULL,READONLY,1))!=OK) return ret;
if ((ret=set_variable("$lport",NULL,port,1))!=OK) return ret;
sprintf(port,"%d",current_pcs->remote_port);
if ((ret=create_variable("$rport",NULL,NULL,READONLY,1))!=OK) return ret;
if ((ret=set_variable("$rport",NULL,port,1))!=OK) return ret;

/* Setup standard IO stuff */
if ((ret=create_stream("STDIN",NULL,0,-1,READ,current_pcs))!=OK) return ret;
if ((ret=create_stream("STDOUT",NULL,1,-1,WRITE,current_pcs))!=OK) return ret;

/* Create message queue stream. The dummy stream name MESG_Q will be
   mapped onto the processes pid named stream by get_stream() when a process 
   uses it */
if ((ret=create_stream(pid,NULL,-1,MESG_Q,READWRITE,current_pcs))!=OK) return ret;

/* Set default input and output */
set_iostreams(INPT,"STDIN");
set_iostreams(OUTPT,"STDOUT");

return OK;
}



/*** Go through words and set aliases. If the alias command is in a proc then
	only set them _within_ the procedure ***/
set_aliases(pc)
int pc;
{
char *ops[]={
"<",">","!","=","<=",">=","!="
};
char *wto,*wfrom;
int pc2,i;

pc2=prog_word[pc].end_pos+1;
if (pc2-pc!=3) return ERR_SYNTAX;

wto=prog_word[pc+1].word;
wfrom=prog_word[pc+2].word;

for(;pc2<num_words;++pc2) {
	if (current_proc && prog_word[pc2].com_num==ENDPROC) return OK;

	if (!strcmp(prog_word[pc2].word,wfrom)) {
		set_string(&prog_word[pc2].word,wto);

		/* See if 'to' word is a command */
		if ((prog_word[pc2].com_num=get_command_number(wto))!=-1) {
			for(i=pc2+1;i<num_words;++i) {
				if (prog_word[i].word[0]==']' ||
				    prog_word[i].prog_line!=prog_word[pc2].prog_line) break;
				}
			prog_word[pc2].end_pos=i-1;
			continue;
			}

		/* See if its an operator (except for square brackets) */
		for(i=0;i<7;++i) {
			if (!strcmp(wto,ops[i])) {
				prog_word[pc2].op_num=i+2;  break;
				}
			}
		}
	}
return OK;
}



/*** Create an entry in the loops linked list ***/
create_loop_entry(type,pos)
int type,pos;
{
struct loops_choose *new;
int p,loop_cnt,len;
int end_word,com_num;

real_line=prog_word[pos].real_line;

/* Check syntax */
len=prog_word[pos].end_pos - pos;
switch(type) {
	case DO:
	if (len>0) return ERR_SYNTAX;
	break;

	case CHOOSE:
	case WHILE :
	if (len<1) return ERR_SYNTAX;
	break;

	default:
	if (len<3) return ERR_SYNTAX;
	}

if ((new=(struct loops_choose *)malloc(sizeof(struct loops_choose)))==NULL) 
	return ERR_MALLOC;
if (first_loop==NULL) first_loop=new; 
else last_loop->next=new;
last_loop=new;
new->type=type;
new->for_to=0;
new->foreach_pos=-1;
new->foreach_var=NULL;
new->start_pos=pos;
new->proc=current_proc;
new->next=NULL;
new->copy=NULL;

/* find end of this loop */
switch(type) {
	case WHILE  : end_word=WEND;  break;
	case FOR    : end_word=NEXT;  break;
	case FOREACH: end_word=NEXTEACH;  break;
	case DO     : end_word=UNTIL;  break;
	case CHOOSE : end_word=CHOSEN;
	}
loop_cnt=0;

/* Find end of loop */
for(p=pos+1;p<num_words;++p) {
	com_num=prog_word[p].com_num;
	if (com_num==ENDPROC) break;
	if (com_num==type) ++loop_cnt;
	if (!loop_cnt && com_num==end_word) {
		/* Make sure no crap follows loop terminator */
		if (end_word!=UNTIL && prog_word[p].end_pos - p >0) {
			real_line=prog_word[p].real_line;
			return ERR_SYNTAX;
			}
		new->end_pos=p;  
		return OK;
		}
	if (com_num==end_word) --loop_cnt;
	}
real_line=prog_word[pos].real_line;
return ERR_UNTERMINATED_LOOP;
}


/*** Create an entry in the labels linked list ***/
create_label_entry(pos)
int pos;
{
struct labels *label;
char *w;

/* Check syntax */
if (prog_word[pos].end_pos - pos!=1) return ERR_SYNTAX;

w=prog_word[pos+1].word;
if (*w=='"') push_rstack_qstr(w);
else {
	if (isinteger(w,0)) push_rstack(w);
	else return ERR_SYNTAX;
	}

/* see if label already used */
for(label=first_label;label!=NULL;label=label->next) {
	if (!strcmp(label->name,rstack_ptr->value)) return ERR_DUPLICATE_LABEL;
	}

if ((label=(struct labels *)malloc(sizeof(struct labels)))==NULL) 
	return ERR_MALLOC;

if (first_label==NULL) first_label=label;
else last_label->next=label;
last_label=label;
label->next=NULL;
label->pos=pos;
label->proc=current_proc;
label->name=NULL;
return set_string(&label->name,rstack_ptr->value);
}


/*** Create an entry in the procedure linked list ***/
create_procedure_entry(pos)
int pos;
{
struct procedures *proc;
char *name;

if (prog_word[pos].end_pos - pos<1) return ERR_SYNTAX;
name=prog_word[pos+1].word;
if (!isalnumstr(name)) return ERR_INVALID_PROCNAME;

/* See if name already used */
for(proc=first_proc;proc!=NULL;proc=proc->next) {
	if (!strcmp(name,proc->name)) return ERR_DUPLICATE_PROCNAME;
	}
if ((proc=(struct procedures *)malloc(sizeof(struct procedures)))==NULL) 
	return ERR_MALLOC;

if (first_proc==NULL) first_proc=proc;
else last_proc->next=proc;
last_proc=proc;
current_proc=proc;

proc->copy=NULL; /* Only used during process spawning */

proc->next=NULL;
proc->start_pos=pos;
proc->ei_di_int=0;
proc->old_int=0;

/* Set proc name */
proc->name=NULL;
return set_string(&proc->name,name);
}




/*****************************************************************************
             CREATE, DESTROY, SET AND GET VARIABLES FUNCTIONS
 *****************************************************************************/

/*** Create an entry in the variables linked list. No values are set. Proc
     is only needed for when we destroy it but we can't have a global and
     local with the same name anyway. ***/
create_variable(name,proc,refvar,flags,sys)
char *name;
struct procedures *proc;
struct vars *refvar;
int flags,sys;
{
struct vars *var;

/* Check its a valid name unless its a SYS variable */
if (!*name || isinteger(name,1) || (!sys && !isalnumstr(name))) 
	return ERR_INVALID_VAR;

/* See if var already exists. If its a static it may well do in which case
   we just ok it. */
for(var=first_var;var!=NULL;var=var->next) {
	if (!strcmp(var->name,name) && (var->proc==proc || var->proc==NULL)) {
		if ((var->flags & STATIC) && (flags & STATIC)) return OK;
		return ERR_VAR_ALREADY_EXISTS;
		}
	}

if ((var=(struct vars *)malloc(sizeof(struct vars)))==NULL) 
	return ERR_MALLOC;

if (first_var==NULL) {
	first_var=var;  var->prev=NULL;
	}
else {
	last_var->next=var;  var->prev=last_var;
	}
last_var=var;
current_var=var;
var->copy=NULL; /* Only used during process spawning */

var->proc=proc;
var->refvar=refvar;
var->flags=flags;
var->value=NULL; 
var->arr=NULL;
var->next=NULL;
var->name=NULL;
return set_string(&var->name,name);
}


/*** Destroy all non-static variables and formal parameters in the given 
     procedure when we exit it. I don't need to do this , I could just set
     the non-static ones to NULL but this'll save a lot of memory in a multi-
     process enviroment. ***/
destroy_proc_variables(proc)
struct procedures *proc;
{
struct vars *var;
struct array *arr,*arr2;

for(var=first_var;var!=NULL;var=var->next) {
	if (var->proc==proc && !(var->flags & STATIC)) {
		if (var->arr!=NULL) {
			/* Clear out the array */
			for(arr=var->arr;arr!=NULL;arr=arr2) {
				arr2=arr->next;
				free(arr->subscript);
				FREE(arr->value);
				free(arr);
				}
			}
		free(var->name);
		FREE(var->value);
		if (var->prev!=NULL) var->prev->next=var->next; 
		if (var->next!=NULL) var->next->prev=var->prev; 
		if (var==first_var)  first_var=var->next;
		if (var==last_var)   last_var=var->prev;
		free(var); 
		}
	}
return OK;
}



/*** Set variable value, decoding array subscript if necessary ***/
set_variable(name,proc,value,force)
char *name,*value;
struct procedures *proc;
int force; /* Force it to write to a read only var */
{
struct process *pcs,*old;
struct vars *var;
char *subscript,*tmp,*c,*start;
int ret,name_len,sub_type,i,shared;

sub_type=0;
subscript=NULL;
name_len=strlen(name);

/* See if we're accessing remote shared variable */
shared=0;
c=name;
while(*c) {
	if (*c==':' || *c=='#' || *c=='?') break; 
	if (*c=='.') {  shared=1;  break;  }
	++c;
	}
if (shared) { 
	*c='\0';
	if (!strlen(name)) {
		*c='.';  return ERR_SYNTAX;
		}
	if (isinteger(name,0)) {
		if ((pcs=get_process(atoi(name)))==NULL) {
			*c='.';  return ERR_NO_SUCH_PROCESS;
			}
		}
	else {
		if ((ret=get_variable(name,proc))!=OK) { 
			*c='.';  return ret;
			}
		if ((pcs=get_process(atoi(rstack_ptr->value)))==NULL) {
			*c='.';  return ERR_NO_SUCH_PROCESS;
			}
		}
	*c='.';
	start=name;
	name=c+1;	
	if (!strlen(name)) return ERR_SYNTAX;
	name_len=strlen(name);
	old=current_pcs;
	save_process_state(old,0);
	load_process_state(pcs);
	}

/* Find the beginning of the subscript after ':' */
if ((subscript=(char *)strchr(name,':'))!=NULL) {
	name_len=(int)(subscript-name);  subscript++;
	}
if ((tmp=(char *)strchr(name,'#'))!=NULL) {
	if (subscript==NULL || tmp<subscript) {
		name_len=(int)(tmp-name);
		subscript=++tmp;
		sub_type=1;
		}
	}
if ((tmp=(char *)strchr(name,'?'))!=NULL) {
	if (subscript==NULL || tmp<subscript) {
		name_len=(int)(tmp-name);
		subscript=++tmp;
		sub_type=2;
		}
	}

/* Go through backwards so we encounter local vars first. If no local 
   variable found try system variable. */
for(i=0;i<2;++i) {
	for(var=last_var;var!=NULL;var=var->prev) {
		if (!strncmp(var->name,name,name_len) &&
		    strlen(var->name)==name_len &&
		    (var->proc==proc || var->proc==NULL)) goto FOUND;
		}
	if (shared) break;
	if (!i) {
		old=current_pcs;
		save_process_state(old,0);
		load_process_state(first_pcs);
		}
	}
load_process_state(old);
return ERR_UNDEF_VAR;

FOUND:
/* Load old process back , "var" will be unaffected */
if (i) load_process_state(old);
if (shared) {
	name=start;
	load_process_state(old);
	if (!(var->flags & SHARED)) return ERR_VAR_NOT_SHARED;
	}
current_var=var;
if ((var->flags & READONLY) && !force) return ERR_VAR_IS_READONLY; 

/* If variable is reference var go back until we find variable pointed to.
   We loop here as a pass by reference could be referencing another pass by
   reference and so on */
if (var->refvar) {
	while(var->refvar!=NULL) var=var->refvar;
	if ((var->flags & READONLY) && !force) return ERR_VAR_IS_READONLY;
	}

if (subscript==NULL) {
	if (var->flags & ARRAY) return set_array_by_list(var,proc,value);
	else return set_string(&var->value,value);
	}
if (!(var->flags & ARRAY)) return ERR_VAR_IS_NOT_ARRAY;
return set_array(var,subscript,sub_type,proc,value);
}



/*** Set an array using an input list eg "hello cruel world". "hello" would
     become element 1 , "cruel" element 2 and so on. Any subscripts already
     in the array will be preserved *if* their slot is needed. If no slot
     already exists a number subscript is given to it. ***/
set_array_by_list(var,proc,value)
struct vars *var;
char *value;
{
struct array *arr,*arrnext,*arrlast;
char *s,*s2,c;
int i,ret,arrind;

s=value;
arr=var->arr;
arrind=1;

/* Repopulate array */
while(value && 1) {
	/* Find beginning of element in list */
	while(*s<33 && *s) ++s;
	if (!*s) break;

	/* Find end of element */
	s2=s;
	while(*s2>33) ++s2;
	c=*s2;  *s2='\0';

	/* Find current array struct */
	arr=var->arr;
	for(i=1;i<arrind && arr;++i) arr=arr->next;

	/* Set array element. */
	if (arr) sprintf(text2,"\"%s\"",arr->subscript);
	else sprintf(text2,"\"%d\"",arrind);
	if ((ret=set_array(var,text2,0,proc,s))!=OK) return ret;

	/* Set for next loop */
	arrind++;
	*s2=c;  s=s2;
	}
if (arr==NULL) return OK;

/* Delete anything we don't need */
i=0;
arrlast=NULL;
for(arr=var->arr;arr!=NULL;arr=arrnext) {
	arrnext=arr->next;
	if (++i<arrind) {  arrlast=arr;  continue;  }
	if (i==arrind && arrlast) arrlast->next=NULL;
	free(arr->subscript);
	FREE(arr->value);
	free(arr);
	}
if (arrind==1) var->arr=NULL;
return OK;
}



/*** Set array entry value. 
     Sub_type: 0 = lookup array subscript
               1 = lookup by index number (indexes start at _1_ not 0)
               2 = reverse lookup by value ***/
set_array(var,subscript,sub_type,proc,value)
struct vars *var;
struct procedures *proc;
char *subscript,*value;
int sub_type;
{
struct array *arr,*arr2;
int subint,cnt,len,ret;

/* Get subscript value */
ret=-1;
len=0;
subint=-1;
if (sub_type==1 && isinteger(subscript,0)) subint=atoi(subscript);
else {
	/* Subscript is a quoted string */
	if (*subscript=='"') {
		if (sub_type==1) return ERR_INVALID_ARGUMENT;
		subscript++;
		len=strlen(subscript);
		if (subscript[len-1]!='"' || len<2) return ERR_SYNTAX;
		len--;
		}
	else {
		/* Subscript is a variable itself */
		if ((ret=get_variable(subscript,proc))!=OK) return ret;
		if (rstack_ptr->value==NULL) return ERR_INVALID_SUBSCRIPT;
		if (sub_type==1) {
			if (!isinteger(rstack_ptr->value,0) ||
			    !(subint=atoi(rstack_ptr->value))) 
				return ERR_INVALID_ARGUMENT;
			}
		else {
			subscript=rstack_ptr->value;	
			len=strlen(subscript);
			}
		}
	}

/* Go through array elements */
cnt=1; 
arr2=NULL;
for(arr=var->arr;arr!=NULL;arr=arr->next) {
	switch(sub_type) {
		case 0: /* Normal lookup */
		if (!strncmp(arr->subscript,subscript,len) &&
		    strlen(arr->subscript)==len) goto FOUND;
		break;

		case 1: /* Integer lookup */
		if (cnt==subint) goto FOUND; 
		break;

		case 2: /* Reverse lookup */
		if (!strncmp(arr->value,subscript,len) &&
		    strlen(arr->value)==len) goto FOUND;
		}
	arr2=arr;  
	cnt++;
	}

/* If we've not found it then either give error if doing a number lookup
   or reverse lookup or drop through and create new entry */
if (sub_type) {
	if (sub_type==1) return ERR_UNDEF_SUBSCRIPT;
	else return ERR_ELEMENT_NOT_FOUND; /* if reverse lookup */
	}

FOUND:
if (arr!=NULL) return set_string(&arr->value,value);
if ((arr=(struct array *)malloc(sizeof(struct array)))==NULL) return ERR_MALLOC;
arr->subscript=NULL;
arr->value=NULL;
arr->next=NULL;

set_string(&arr->subscript,subscript);
len=strlen(subscript)-1;
if (subscript[len]=='"') arr->subscript[len]='\0';
if (var->arr==NULL) var->arr=arr;
else arr2->next=arr;

return set_string(&arr->value,value);
}



/*** Copy the contents and subscripts of v1 into v2 ***/
copy_array(v1,v2)
struct vars *v1,*v2;
{
struct array *a1,*a2,*aprev;

aprev=NULL;
a2=v2->arr;
for(a1=v1->arr;a1!=NULL;a1=a1->next) {
	if (a2==NULL) {
		if ((a2=(struct array *)malloc(sizeof(struct array)))==NULL) 
			return ERR_MALLOC;
		a2->subscript=NULL;
		a2->value=NULL;
		a2->next=NULL;
		if (aprev!=NULL) aprev->next=a2;
		if (v2->arr==NULL) v2->arr=a2;
		}
	set_string(&a2->subscript,a1->subscript);
	set_string(&a2->value,a1->value);
	aprev=a2;
	a2=a2->next;
	}
return OK;
}



/*** Get variable value. Result is placed on top of the stack which calling
     function must clear itself. ***/
get_variable(name,proc)
char *name;
struct procedures *proc;
{
struct process *pcs,*old;
struct vars *var;
char *subscript,*tmp,*c,*start;
int ret,name_len,sub_type,i,shared;

sub_type=0;
subscript=NULL;
name_len=strlen(name);

/* See if we're accessing remote shared variable (make sure the dot is not
   part of the subscript) */
shared=0;
c=name;
while(*c) {
	if (*c==':' || *c=='#' || *c=='?') break; 
	if (*c=='.') {  shared=1;  break;  }
	++c;
	}
if (shared) { 
	*c='\0';
	if (!strlen(name)) {
		*c='.';  return ERR_SYNTAX;
		}
	if (isinteger(name,0)) {
		if ((pcs=get_process(atoi(name)))==NULL)  {
			*c='.';  return ERR_NO_SUCH_PROCESS;
			}
		}
	else {
		if ((ret=get_variable(name,proc))!=OK) {
			*c='.';  return ret;
			}
		if ((pcs=get_process(atoi(rstack_ptr->value)))==NULL) {
			*c='.';  return ERR_NO_SUCH_PROCESS;
			}
		}
	*c='.';
	start=name;
	name=c+1;	
	if (!strlen(name)) return ERR_SYNTAX;
	name_len=strlen(name);
	old=current_pcs;
	save_process_state(old,0);
	load_process_state(pcs);
	}

/* Find the beginning of the subscript after ':' */
if ((subscript=(char *)strchr(name,':'))!=NULL) {
	name_len=(int)(subscript-name);  subscript++;
	}
if ((tmp=(char *)strchr(name,'#'))!=NULL) {
	if (subscript==NULL || tmp<subscript) {
		name_len=(int)(tmp-name);
		subscript=++tmp;
		sub_type=1;
		}
	}
if ((tmp=(char *)strchr(name,'?'))!=NULL) {
	if (subscript==NULL || tmp<subscript) {
		name_len=(int)(tmp-name);
		subscript=++tmp;
		sub_type=2;
		}
	}

/* Go through backwards so we encounter local vars first. If no local 
   variable found try system variable. */
for(i=0;i<2;++i) {
	for(var=last_var;var!=NULL;var=var->prev) {
		if (!strncmp(var->name,name,name_len) &&
		    strlen(var->name)==name_len &&
		    (var->proc==proc || var->proc==NULL)) goto FOUND;
		}
	if (shared) break;
	if (!i) {
		old=current_pcs;
		save_process_state(old,0);
		load_process_state(first_pcs);
		}
	}
load_process_state(old);
return ERR_UNDEF_VAR;

FOUND:
/* Load old process back , "var" will be unaffected */
if (i) load_process_state(old);
if (shared) {
	name=start;
	load_process_state(old);
	if (!(var->flags & SHARED)) return ERR_VAR_NOT_SHARED;
	}
	
current_var=var;

/* If variable is reference var go back until we find variable pointed to.
   We loop here as a pass by reference could be referencing another pass by
   reference and so on */
while(var->refvar!=NULL) var=var->refvar;

if (subscript==NULL) {
	if (var->flags & ARRAY) return get_array_as_list(var);
	return push_rstack(var->value);
	}
if (!(var->flags & ARRAY)) return ERR_VAR_IS_NOT_ARRAY;
return get_array(var,subscript,sub_type,proc);
}



/*** Get all the elements in the array and append them together as one 
     string/list. ***/
get_array_as_list(var)
struct vars *var;
{
struct array *arr;
char *str;

str=NULL;

/* Each list entry gets a space before and after it making searching
   for elements at the start and end using instr easier */
for(arr=var->arr;arr!=NULL;arr=arr->next) {
	append_string(&str,arr->value);  append_string(&str," ");
	}
if (str) str[strlen(str)-1]='\0'; /* Remove trailing space */
push_rstack(str);
FREE(str);
return OK;
}



/*** Get value of array entry or subscript (reverse lookup).
     Sub_type: 0 = lookup by array subscript
               1 = lookup by index number (indexes start at 1 *not* 0)
               2 = reverse lookup by value ***/
get_array(var,subscript,sub_type,proc)
struct vars *var;
char *subscript;
struct procedures *proc;
int sub_type;
{
struct array *arr;
int subint,cnt,len,ret;

/* Get subscript value */
ret=-1;
len=0;
subint=-1;
if (sub_type && isinteger(subscript,0)) subint=atoi(subscript); 
else {
	/* Subscript is a quoted string */
	if (*subscript=='"') {
		if (sub_type==1) return ERR_SYNTAX;
		subscript++;
		len=strlen(subscript);
		if (subscript[len-1]!='"' || len<2) return ERR_SYNTAX;
		len--;
		}
	else {
		/* Subscript is a variable itself */
		if ((ret=get_variable(subscript,proc))!=OK) return ret;
		if (rstack_ptr->value==NULL) return ERR_UNDEF_SUBSCRIPT;
		if (sub_type==1) {
			if (!isinteger(rstack_ptr->value,0) ||
			    !(subint=atoi(rstack_ptr->value))) 
				return ERR_INVALID_ARGUMENT;
			}
		else {
			subscript=rstack_ptr->value;
			len=strlen(subscript);
			}
		}
	}

/* Go through array elements and get value */
cnt=1; 
for(arr=var->arr;arr!=NULL;arr=arr->next) {
	switch(sub_type) {
		case 0: /* Normal lookup */
		if (!strncmp(arr->subscript,subscript,len) &&
		    strlen(arr->subscript)==len)
			return push_rstack(arr->value); 
		break;

		case 1: /* Number lookup */
		if (cnt==subint) return push_rstack(arr->value); 
		break;

		case 2: /* Reverse lookup */
		if (arr->value!=NULL && 
		    !strncmp(arr->value,subscript,len) &&
		    strlen(arr->value)==len) 
			return push_rstack(arr->subscript);
		}
	cnt++;
	}
if (sub_type==2) return push_rstack(NULL);
return ERR_UNDEF_SUBSCRIPT;
}


/*** Set up initial argc and argv parameters for start proc. Don't include
     argv[0] as that is the initial program filename. ***/
setup_arg_vars(argc,argv)
int argc;
char *argv[];
{
int pc,prog_line;
int i,cnt,flags,ret;
char *var,*str;

cnt=0;
pc=first_proc->start_pos+2;
prog_line=prog_word[pc-2].prog_line;
real_line=prog_word[pc-2].real_line;

/* Go through and create internal argc and argv using whatever parameter
   names we find */
while(prog_word[pc].prog_line==prog_line) {
	if (cnt==2) return ERR_PARAM_COUNT;

	var=prog_word[pc].word;
	if (*var=='@') {  var++;  flags=ARRAY;  } else flags=0;
	if ((ret=create_variable(var,first_proc,NULL,flags,0))!=OK)
		return ret;

	/* Create argc */
	if (!cnt) {
		sprintf(text,"%d",argc-1);
		if ((ret=set_variable(var,first_proc,text,1))!=OK) return ret;
		cnt++;  pc++;
		continue;
		};

	/* Create argv as a string/list or as an array with subscripts being 
	  1 to argc */
	str=NULL;
	for(i=1;i<argc;++i) {
		append_string(&str,argv[i]);  append_string(&str," ");
		}
	if (str) str[strlen(str)-1]='\0'; /* Remove trailing space */
	if ((ret=set_variable(var,first_proc,str,1))!=OK) return ret;
	cnt++;  pc++;
	}
return OK;
}
		
		
		


/*****************************************************************************
                              STACK FUNCTIONS 
 *****************************************************************************/

/*** Push a value onto the result stack. The return value isn't always 
     checked simply because the function is called so often. ***/
push_rstack(value)
char *value;
{
struct result_stack *stk;

if ((stk=(struct result_stack *)malloc(sizeof(struct result_stack)))==NULL)
	return ERR_MALLOC;

if (value!=NULL) {
	if ((stk->value=(char *)malloc(strlen(value)+1))==NULL) {
		free(stk);  return ERR_MALLOC;
		}
	strcpy(stk->value,value);
	}
else stk->value=NULL;

if (rstack_start==NULL) {
	rstack_start=stk;  stk->prev=NULL;
	}
else {
	rstack_ptr->next=stk;  stk->prev=rstack_ptr;
	}
stk->next=NULL;
rstack_ptr=stk;
return OK;
}


/*** Get result of an argument to a command and push it onto the stack. 
     ALL_LEGAL     : any result acceptable.
     STRING_ILLEGAL: non-numeric string illegal.
     INT_ILLEGAL   : non-string number hardcoded into the program is illegal. 
 ***/
push_rstack_result(pc,legal)
int *pc,legal;
{
char *w;
int ret,tmp;

w=prog_word[*pc].word;
switch(*w) {
	case '[': 
	tmp=++*pc;
	if ((ret=exec_command(&tmp))!=OK) return ret;
	*pc=prog_word[*pc].end_pos+2;
	break;

	case '"': 
	if (legal==STRING_ILLEGAL) return ERR_INVALID_ARGUMENT;
	push_rstack_qstr(w);  ++*pc;  
	return OK;

	default:
	if (isinteger(w,1)) {
		if (legal==INT_ILLEGAL) return ERR_INVALID_ARGUMENT;
		++*pc;  return push_rstack(w);
		}
	else if ((ret=get_variable(w,current_proc))!=OK)  return ret;
	++*pc;
	}
if (legal==STRING_ILLEGAL && !isinteger(rstack_ptr->value,1))
	return ERR_INVALID_ARGUMENT;
return OK;
}



/*** Push a quoted string onto the result stack after removing the quotes */
push_rstack_qstr(str)
char *str;
{
int len;

str++;  
len=strlen(str)-1;  
if (!len) push_rstack(NULL);
else {
	str[len]='\0';		
	push_rstack(str);
	str[len]='"'; 
	}
}



/*** Remove top entry off result stack ***/
pop_rstack()
{
struct result_stack *stk;

if (rstack_ptr==NULL) return OK;
stk=rstack_ptr;
rstack_ptr=stk->prev;
if (stk==rstack_start) rstack_start=NULL;
else rstack_ptr->next=NULL;
FREE(stk->value);
free(stk);		
return OK;
}


/*** Push proc entry onto proc stack ***/
push_pstack(proc)
struct procedures *proc;
{
struct proc_stack *stk;

if ((stk=(struct proc_stack *)malloc(sizeof(struct proc_stack)))==NULL)
	return ERR_MALLOC;
stk->proc=proc;
stk->ret_pc=0; /* This is set by com_call() function */
if (pstack_start==NULL) {
	pstack_start=stk;  stk->prev=NULL;
	}
else {
	pstack_ptr->next=stk;  stk->prev=pstack_ptr;
	}
stk->next=NULL;
pstack_ptr=stk;
return OK;
}


/*** Remove top entry off proc stack ***/
pop_pstack()
{
struct proc_stack *stk;

if (pstack_ptr==NULL) return OK;
stk=pstack_ptr;
pstack_ptr=stk->prev;
if (stk==pstack_start) pstack_start=NULL;
else pstack_ptr->next=NULL;
free(stk);		
return OK;
}


/*** Push loop entry onto loop stack ***/
push_lstack(loop)
struct loops_choose *loop;
{
struct loop_stack *stk;

if ((stk=(struct loop_stack *)malloc(sizeof(struct loop_stack)))==NULL)
	return ERR_MALLOC;
stk->loop=loop;
if (lstack_start==NULL) {
	lstack_start=stk;  stk->prev=NULL;
	}
else {
	lstack_ptr->next=stk;  stk->prev=lstack_ptr;
	}
stk->next=NULL;
lstack_ptr=stk;
return OK;
}


/*** Remove top entry off loop stack ***/
pop_lstack()
{
struct loop_stack *stk;

if (lstack_ptr==NULL) return OK;
stk=lstack_ptr;
lstack_ptr=stk->prev;
if (stk==lstack_start) lstack_start=NULL;
else lstack_ptr->next=NULL;
free(stk);		
return OK;
}






/*****************************************************************************
                     EVALUATE BOOLEAN EXPRESSIONS 
 *****************************************************************************/
/*** Evaluate the complete X and Y or Z etc clause ***/
evaluate_complete_expression(pc)
int pc;
{
int end,res,op;
char *w;

end=prog_word[pc].end_pos;
++pc;
op=0;
eval_result=0;

while(1) {
	if ((res=evaluate_single_expression(&pc))>0) return res;
	res=-res; /* switch -1 back to 1 */
	
	/* As true and res are only ever 1 or 0 we can get away with using
	   bitwise ops here which is usefull for XOR as there is no logical
	   operator for this in C. Bit of an oversight there by K&R methinks */
	switch(op) {
		case 0: eval_result=res;   break;
		case 1: eval_result&=res;  break; /* AND */
		case 2: eval_result|=res;  break; /* OR */
		case 3: eval_result^=res;  /* XOR */
		}
	w=prog_word[pc].word;
	if (!strcmp(w,"and")) op=1;
		else if (!strcmp(w,"or")) op=2;
			else if (!strcmp(w,"xor")) op=3;
				else if (pc==end+1) return OK;
					else return ERR_SYNTAX;
	++pc;
	}
}
	


/*** This evalutes a single expression clause eg 2 < 3 , "tom" != "dick" ***/
evaluate_single_expression(pc)
int *pc;
{
int cnt,ret,op,cmp,val0,val1,isint_val0,isint_val1;
char *valptr[2];

cnt=0;

/* Get the 2 values in the expression */
while(1) {
	if (cnt==1) {
		if ((op=prog_word[*pc].op_num)==-1) break;
		++*pc;  
		}
	if ((ret=push_rstack_result(pc,ALL_LEGAL))!=OK) return ret;
	valptr[cnt]=rstack_ptr->value;
	if (++cnt==2) break;
	}

/* See if a not NULL or nonzero test (ie no operator given) */
if (op==-1) {
	if (isnull(valptr[0])) return 0;
	if (isinteger(valptr[0],1) && !atoi(valptr[0])) return 0;
	return -1;
	}

/* Got the 2 values , now get int values */
if (isinteger(valptr[0],1)) {  
	val0=atoi(valptr[0]);  isint_val0=1;
	}
else isint_val0=0;

if (isinteger(valptr[1],1)) {  
	val1=atoi(valptr[1]);  isint_val1=1;
	}
else isint_val1=0;

/* Get string compare */
cmp=compare_strings(valptr[0],valptr[1]);

/* Compare the 2 values */
switch(op) {
	case LESS_THAN:
	if (isint_val0 && isint_val1) return (val0<val1 ? -1 : 0);
	else return (cmp<0 ? -1 : 0);

	case GREATER_THAN:
	if (isint_val0 && isint_val1) return (val0>val1 ? -1 : 0);
	else return (cmp>0 ? -1 : 0);

	case LESS_THAN_EQUALS:
	if (isint_val0 && isint_val1) return (val0<=val1 ? -1 : 0);
	else return (cmp<=0 ? -1 : 0);

	case GREATER_THAN_EQUALS:
	if (isint_val0 && isint_val1) return (val0>=val1 ? -1 : 0);
	else return (cmp>=0 ? -1 : 0);

	case EQUALS:
	if (isint_val0 && isint_val1) return (val0==val1 ? -1 : 0);
	else return (!cmp ? -1 : 0);

	case NOT:
	case NOT_EQUAL:
	if (isint_val0 && isint_val1) return (val0!=val1 ? -1 : 0);
	else return (cmp ? -1 : 0);
	}
/* Should never get here */
return ERR_INTERNAL;
}



/******************************************************************************
                              RUNTIME FUNCTIONS
    Theres are the functions that run the actual AviosPL language commands.
 ******************************************************************************/

/*** Main runtime function for the processes which contains the simple 
     time-slice code. ***/
exec_process(pc,setup_globals)
int pc,setup_globals;
{
int pc2,in_proc,com_num;
int ret,inflag;

in_proc=0;
if (setup_globals) pc=0; else in_proc=1;

/* Main interpreter loop */
while(1) {
	real_line=prog_word[pc].real_line;
	com_num=prog_word[pc].com_num;

	if (com_num==PROC) in_proc=1;
	else if (com_num==ENDPROC) in_proc=0;

	/* Only execute "var" and "set" commands and only if they are outside 
	   procedures */
	if (setup_globals) {
		if (prog_word[pc].com_num==-1) return ERR_SYNTAX;

		if (in_proc) {
			pc=prog_word[pc].end_pos+1;  continue;
			}

		switch(com_num) {
			case ALIAS  : pc=prog_word[pc].end_pos+1;  break;

			case VAR: 
			case SVAR:
			if ((ret=com_var(com_num,pc))!=OK) return ret;
			pc=prog_word[pc].end_pos+1;
			break;

			case SHARE:
			case UNSHARE:
			if ((ret=com_sharing(com_num,pc))!=OK) return ret;
			pc=prog_word[pc].end_pos+1;
			break;  

	    		case SET: 
			if ((ret=com_sid(SET,pc))!=OK) return ret;
			pc=prog_word[pc].end_pos+1;
			break;  

			case ENDPROC: ++pc;  break;

			default: return ERR_UNEXPECTED_COMMAND;
			}
		if (pc>=num_words) return OK;
		continue;
		}

	/* See if we've executed max number of instructions for this process
	   in this timeslot. We do this before exec_command in case it executed
	   way too many last time due to nested commands. */
	if (inst_cnt>=swapout_after - current_pcs->over) {
		current_pcs->over+=inst_cnt - swapout_after;
		if (current_pcs->over<0) current_pcs->over=0;
		current_pcs->pc=pc;  
		return OK;
		}

	/* Run a single command (which may recursively call nested commands) */
	pc2=pc;
	inflag=(current_pcs->status==INPUT_WAIT);
	if ((ret=exec_command(&pc))!=OK) {
		current_pcs->pc=pc;
		if (!max_errors || ++current_pcs->err_cnt<max_errors) {
			sprintf(text,"ERROR: Process %d (%s): ",current_pcs->pid,current_pcs->name);
			sprintf(text,"%s%s in file '%s' on line %d; ignoring...",text,error_mesg[ret],prog_word[pc].filename,real_line);
			write_syslog(text);
			}
		else return ret;
		}

	/* Check some flags */
	switch(current_pcs->status) {
		case OUTPUT_WAIT:
		case CHILD_DWAIT:
		case SPEC_DWAIT :  
		case SLEEPING   :
		/* If we're waiting on a process termination or sleeping
		   then return */
		current_pcs->pc=pc;  return OK;

		case INPUT_WAIT:
		/* If we're still awaiting input and stream is blocking then 
		   return. Also if stream is non blocking but INPUT_WAIT flag 
		   has only just been set up com_input return too. */
		if (current_instream->block || !inflag) {
			current_pcs->pc=pc;  return OK;
			}
		break;

		case EXITING  : return OK;
		}

	/* If command hasn't set pc set it here to next command unless its an
	   ENDPROC because then we'll have finished */
	if (pc2==pc && com_num!=ENDPROC) pc=prog_word[pc].end_pos+1;

	/* Save process counter */
	current_pcs->pc=pc;

	/* See if we've reached the end of the program */
	if (pc>=num_words || current_proc==NULL) {
		current_pcs->status=EXITING;  
		current_pcs->exit_code=OK;  
		return OK;
		}

	/* In the unlikely event that the process has interrupted itself */
	if (current_pcs->status==NONCHILD_INT) return OK;

	/* See if we've moved out of our loop due to a goto or an if 
	   (eg: if i=2; next; endif)
	   Coming out of loops via a break, return or endproc is dealt with
	   in the appropriate functions. */
	while(1) {
		if (current_loop!=NULL &&
		    current_loop->proc==current_proc && 
		    (pc<current_loop->start_pos || pc>current_loop->end_pos)) {
			current_loop->foreach_pos=-1;
			current_loop->foreach_var=NULL;
			pop_lstack();
			if (lstack_ptr==NULL) {  current_loop=NULL;  break;  }
			else current_loop=lstack_ptr->loop;
			}
		else break;
		}
	}
}



/*** Main function that gets called to execute one command ***/
exec_command(pc)
int *pc;
{
int com_num,ret;
static int nest_level=-1;

nest_level++;
com_num=prog_word[*pc].com_num;
real_line=prog_word[*pc].real_line;

if (com_num==-1) {  ret=ERR_SYNTAX;  goto END;  }

/* Check for invalid nesting. This relies on the command enum ordering being
   in a certain way. This looks a mess. Tough. */
if (nest_level &&
    (com_num<=UNSHARE || 
     com_num==ONINT ||
     (com_num>=PRINT && com_num<=INPUT) || 
     (com_num>=SPAWN && com_num<=WAITPID))) {
	ret=ERR_NESTED_COMMAND;  goto END;
	}

/* Main execution part */
ret=OK;
switch(com_num) {
	case ALIAS: break;

    	case VAR : 
	case SVAR: ret=com_var(com_num,*pc);  break;

	case SHARE  :
	case UNSHARE: ret=com_sharing(com_num,*pc);  break;

    	case SET   : 
	case INC   :
	case DEC   : ret=com_sid(com_num,*pc);  break;

	case NOTCOM:
	case ABS   :
	case SGN   : 
	case RAND  : ret=com_maths1(com_num,*pc);  break;

	case MULSTR: ret=com_strings2(com_num,*pc);  break;
	case SUBSTR: ret=com_strings3(com_num,*pc);  break;
    	case ADDSTR : 
	case ADD    :
	case SUB    :
    	case MUL    :
    	case DIV    : 
	case MOD    : 
	case ATOC   :
	case CTOA   :
	case MAXCOM :
	case MINCOM : 
	case MAXSTR :
	case MINSTR : 
	case PRINT  : 
	case PRINTNL: ret=com_mathsv(com_num,*pc);  break;

    	case GOTO : ret=com_goto(pc);  break;
    	case LABEL: *pc=prog_word[*pc].end_pos+1;  break;

	case IF   : ret=com_if(pc);  break;
	case ELSE : ret=com_else(pc);  break;
	case ENDIF: break;

    	case WHILE   : ret=com_while(pc);  break;
    	case WEND    : ret=com_wend_next(pc);  break;
	case DO      : ret=com_do(*pc);  break;
	case UNTIL   : ret=com_until(pc);  break;
    	case FOR     : ret=com_for(pc);  break;
	case FOREACH : ret=com_foreach(pc);  break;
    	case NEXT    : 
	case NEXTEACH: ret=com_wend_next(pc);  break;

	case CHOOSE : ret=com_choose(pc);  break;
	case VALUE  : 
	case DEFAULT: ret=com_val_def();  break;

	case CHOSEN : ret=com_chosen(pc,real_line); break;

    	case BREAK   : ret=com_break(pc,real_line);  break;
    	case CONTINUE: ret=com_continue(pc,real_line);  break;

	case CALL    : 
	if (nest_level) ret=ERR_NESTED_COMMAND; else ret=com_call(pc);  
	break;

	case PROC   : break;
    	case ENDPROC: 
    	case RETURN : ret=com_return_endproc(com_num,pc);  break;

	case EXIT   : 
	case SLEEP  : ret=com_exit_sleep(com_num,*pc);  break;

	case INPUT: ret=com_input(*pc);   break;

	case STRLEN  : 
	case ISNUM   : 
	case UPPERSTR:
	case LOWERSTR: ret=com_strings1(com_num,*pc);  break;

	case INSTR     :
	case MIDSTR    : 
	case INSERTSTR : 
	case OVERSTR   : 
	case LPADSTR   :
	case RPADSTR   : 
	case INSERTELEM:
	case OVERELEM  :
	case MEMBER    : 
	case SUBELEM   :
	case ELEMENTS  : ret=com_strings3(com_num,*pc);  break;
	case COUNT     : ret=com_count(*pc);  break;
	case MATCH     : ret=com_strings2(com_num,*pc);  break;
	case UNIQUE    :
	case HEAD      :
	case RHEAD     :
	case TAIL      : 
	case RTAIL     : ret=com_strings1(com_num,*pc);  break;

	case ARRSIZE: ret=com_arrsize(*pc);  break;

	case TRAP: ret=com_trap(*pc);  break;

	case IN      :
	case OUT     : 
	case BLOCK   :
	case NONBLOCK: 
	case LOCK    :
	case UNLOCK  : ret=com_streams(com_num,*pc);  break;

	case OPEN  : ret=com_open(*pc);  break;
	case CLOSE : 
	case DELETE: ret=com_close_delete(com_num,*pc);  break;
	case RENAME: 
	case COPY  : ret=com_strings2(com_num,*pc);  break;
	case CSEEK : 
	case LSEEK : ret=com_seek(com_num,*pc);  break;
	case DIRCOM: ret=com_dir(*pc);  break;

	case FORMAT: ret=com_format(*pc);  break;

	case CRYPT: ret=com_strings2(com_num,*pc);  break;

	case SPAWN   : ret=com_spawn(*pc);  break;
	case WAIT    : ret=com_wait(*pc);  break;
	case WAITPID : ret=com_waitpid(*pc);  break;
	case EXISTS  : 
	case PCSINFO : 
	case RELATION: 
	case KILL    : ret=com_eprk(com_num,*pc);  break;
	case EXEC    : ret=com_exec(*pc);  break;

	case ONINT    : ret=com_onint(*pc);  break;
	case INTERRUPT: ret=com_interrupt(*pc);  break;
	case TIMER    : ret=com_timer(*pc);  break;
	case EI       :
	case DI       : ret=com_ei_di(com_num,*pc);  break;

	case GETTIME: ret=com_gettime(*pc);  break;

	case COLOUR: ret=com_colour(*pc);  break;

	case CONNECT: ret=com_connect(*pc);  break;
	case ECHO   : ret=com_echo(*pc);  break;
	}
inst_cnt++;

/* Clear out result stack if back at top level. This saves having to remember
   to pop the stack in every command function if that executed a nested
   command itself. */
END:
if (--nest_level==-1) {
	while(rstack_ptr) pop_rstack();
	}
return ret;
}



/*** Function for "var" and "svar" (static variable) commands ***/
com_var(com_num,pc)
int com_num,pc;
{
int pc2,flags,ret;
char *var;

for(pc2=pc+1;pc2<=prog_word[pc].end_pos;++pc2) {
	var=prog_word[pc2].word;
	if (*var=='@') {  var++;  flags=ARRAY;  } else flags=0;
	if (com_num==SVAR) flags|=STATIC;
	if ((ret=create_variable(var,current_proc,NULL,flags,0))!=OK) 
		return ret;
	}
return push_rstack(var);
}



/*** Function for "share" and "unshare" commands. ***/
com_sharing(com_num,pc)
int com_num,pc;
{
int pc2,ret;
char *w;

if (prog_word[pc].end_pos - pc<1) return ERR_SYNTAX;
for(pc2=pc+1;pc2<=prog_word[pc].end_pos;++pc2) {
	w=prog_word[pc2].word;

	/* Can't set sharing on a system or another processes variable */
	if (*w=='$' || strchr(w,'.')) return ERR_VAR_SHARE1;
	if ((ret=get_variable(w,current_proc))!=OK) return ret;
	if (current_var->proc!=NULL) return ERR_VAR_SHARE2;
	if (com_num==SHARE) current_var->flags|=SHARED;
	else if (current_var->flags & SHARED) current_var->flags^=SHARED;
	}
return OK;
}



/*** Function for set, inc and dec commands ***/
com_sid(com_num,pc)
int pc;
{
int ret,tmp,val,len,is_var;
struct vars *newvar;
char *var,*w;

is_var=0;
len=prog_word[pc].end_pos - pc;
if (len<1 || (com_num==SET && len<2)) return ERR_SYNTAX;

current_var=NULL;
var=prog_word[pc+1].word;
get_variable(var,current_proc); /* Do this to set newvar */
newvar=current_var;

if (len>1) {
	w=prog_word[pc+2].word;
	switch(*w) {
		case '[': 
		tmp=pc+3;  
		if ((ret=exec_command(&tmp))!=OK) return ret;
		break;

		case '"': push_rstack_qstr(w);  break;

		default:
		if (isinteger(w,1)) push_rstack(w);
		else {
			if ((ret=get_variable(w,current_proc))!=OK) return ret;
			is_var=1;
			}
		}
	}

/* Do specific stuff */
if (com_num==SET) {
	/* Current_var will have been set by get_variable call above */
	if (is_var &&
	    newvar!=NULL &&
	    (newvar->flags & ARRAY) && 
	    (current_var->flags & ARRAY) &&
	    !strcmp(newvar->name,var) && /* make sure its not an elem. */
	    !strcmp(current_var->name,w)) {
		if ((ret=copy_array(current_var,newvar))!=OK) return ret;
		}
	else {
		if ((ret=set_variable(var,current_proc,rstack_ptr->value,0))!=OK) 
			return ret;
		}
	return OK;
	}

/* INC and DEC commands */
if ((ret=get_variable(var,current_proc))!=OK) return ret;
if (!isinteger(rstack_ptr->value,1)) return ERR_INVALID_ARGUMENT;

/* If no increment or dec value given then default to 1 */
if (prog_word[pc].end_pos - pc == 1) val=1;
else {
	if (!isinteger(rstack_ptr->prev->value,1)) return ERR_INVALID_ARGUMENT;
	val=atoi(rstack_ptr->prev->value);
	}

/* Set variable */
if (com_num==INC) sprintf(text,"%d",atoi(rstack_ptr->value)+val);
else sprintf(text,"%d",atoi(rstack_ptr->value)-val);

if ((ret=set_variable(var,current_proc,text,0))!=OK) return ret;
return push_rstack(text);
}



/*** This deals with "not", "abs", "sgn" and "rand". They all only take 
     1 argument. "atoc" isn't a maths command but its convenient to put it 
     here as it takes the same argument type and count. ***/
com_maths1(com_num,pc)
int com_num,pc;
{
int val,ret,legal,neg;
char *valptr;

if (prog_word[pc].end_pos - pc < 1) return ERR_SYNTAX;

++pc;
if (com_num==NOTCOM) legal=ALL_LEGAL; else legal=STRING_ILLEGAL;
if ((ret=push_rstack_result(&pc,legal))!=OK) return ret;
valptr=rstack_ptr->value;
if (valptr!=NULL) val=atoi(valptr); else val=0;

switch(com_num) {
	case NOTCOM:
	if (isnull(valptr) || (isinteger(valptr,1) && !val)) 
		return push_rstack("1");
	return push_rstack("0");

	case ABS:
	if (val<0) val=-val;
	sprintf(text,"%d",val);
	break;

	case SGN:
	if (val<0) val=-1;
	else if (val>0) val=1;
		else val=0;
	sprintf(text,"%d",val);
	break;

	case RAND:
	if (val<0) { neg=-1; val=-val; } else neg=1;
	ret=(rand()%(val+1))*neg;
	sprintf(text,"%d",ret);
	}
return push_rstack(text);
}



/*** Function for printing and maths strings & numbers commands. Its way too 
     long and a bloody mess but its too much hassle to split it up so deal with
     it. They all take a variable number of arguments. The print commands 
     can't be nested and are in this function (even though they're not
     maths commands) as its convienient to put them here. ***/
com_mathsv(com_num,pc)
int com_num,pc;
{
int pc2,ret,tmp,len,cnt,val,mesg_q;
char *w,*result,*valptr,*mm_str,*mesg_buff;
struct streams *out;

/* If output stream is a message queue check if we can write to it at the
   moment , if not then suspend process unless stream is non-blocking in
   which case just return. */
out=current_outstream;
if ((com_num==PRINT || com_num==PRINTNL) && 
    out &&
    ((out->internal==MESG_Q && out->owner->mesg_cnt==max_mesgs) || 
     out->locked)) {
	if (out->block) current_pcs->status=OUTPUT_WAIT;  
	set_variable("$print_ok",NULL,"0",1);
	return OK;
	}
if (com_num==PRINTNL && prog_word[pc].end_pos==pc) return write_stream("\n");  

len=prog_word[pc].end_pos-pc;
if (len<2) {
	if (com_num!=PRINT && com_num!=PRINTNL && com_num!=ATOC && com_num!=CTOA) 
		return ERR_SYNTAX;
	if (len<1 && com_num!=PRINT && com_num!=PRINTNL) return ERR_SYNTAX;
	}

cnt=0;
val=0;
result=NULL;
mm_str=NULL;

/* This is if we're printing to a message queue */
mesg_buff=NULL;
if (current_outstream->internal==MESG_Q) mesg_q=1; else mesg_q=0;

/* Go through commands parameters */
for(pc2=pc+1;pc2<=prog_word[pc].end_pos;) {
	cnt++;
	w=prog_word[pc2].word;

	switch(*w) {
		/** Parameter is a nested command **/
		case '[':
		tmp=pc2+1;
		if ((ret=exec_command(&tmp))!=OK) {
			FREE(mesg_buff);  return ret;
			}
		valptr=rstack_ptr->value;

		if (com_num>=ADD && 
		    com_num<=MINCOM && 
		    com_num!=CTOA && 
		    !isinteger(valptr,1)) return ERR_INVALID_ARGUMENT;

		switch(com_num) {
			case ADDSTR:	
			append_string(&result,valptr);  break;

			case ADD: val+=atoi(valptr); break;

			case SUB:
			if (cnt==1) val=atoi(valptr); else val-=atoi(valptr);
			break;

			case MUL:
			if (cnt==1) val=atoi(valptr); else val*=atoi(valptr);
			break;

			case DIV:
			if (cnt==1) val=atoi(valptr); else val/=atoi(valptr);
			break;

			case MOD:
			if (cnt==1) val=atoi(valptr); else val%=atoi(valptr);
			break;

			case ATOC:
			sprintf(text,"%c",atoi(valptr));
			append_string(&result,text);
			break;

			case CTOA:
			if (valptr==NULL) break;
			while(*valptr) {
				sprintf(text,"%d ",*valptr);  
				append_string(&result,text);
				++valptr;
				}
			break;

			case MAXCOM:
			if (cnt==1) val=atoi(valptr);
			else if (val<atoi(valptr)) val=atoi(valptr);
			break;

			case MINCOM:
			if (cnt==1) val=atoi(valptr);
			else if (val>atoi(valptr)) val=atoi(valptr);
			break;

			case MAXSTR:
			if (cnt==1) set_string(&mm_str,valptr);
			else if (compare_strings(mm_str,valptr)<0)
					set_string(&mm_str,valptr);
			break;

			case MINSTR:
			if (cnt==1) set_string(&mm_str,valptr);
			else if (compare_strings(mm_str,valptr)>0)
					set_string(&mm_str,valptr);
			break;

			case PRINT  : 
			case PRINTNL:
			if (valptr!=NULL) {
				if (mesg_q) append_string(&mesg_buff,valptr);
				else if ((ret=write_stream(valptr))!=OK) 
						return ret;
				}
			break;
			}
		pc2=prog_word[pc2+1].end_pos+2;
		break;
	

		/** Parameter is an embedded program string **/
		case '"':
		if (com_num>=ADD && com_num<=MINCOM && com_num!=CTOA) 
			return ERR_INVALID_ARGUMENT;

		/* Parameter is a string */
		w++;  len=strlen(w)-1;  w[len]='\0';		
	
		switch(com_num) {
			case ADDSTR : append_string(&result,w);  break;

			case CTOA:
			while(*w) {
				sprintf(text,"%d ",*w);  
				append_string(&result,text);
				++w;
				}
			break;

			case MAXCOM:
			case MAXSTR:
			if (cnt==1) set_string(&mm_str,w);
			else if (compare_strings(mm_str,w)<0)
					set_string(&mm_str,w);
			break;

			case MINSTR:
			if (cnt==1) set_string(&mm_str,w);
			else if (compare_strings(mm_str,w)>0)
					set_string(&mm_str,w);
			break;

			case PRINT  : 
			case PRINTNL: 
			if (mesg_q) append_string(&mesg_buff,w);
			else if ((ret=write_stream(w))!=OK) return ret;
			break;
			}

		w[len]='"';   ++pc2;
		break;
	

		/** Parameter is an integer or a variable **/
		default:
		if (isinteger(w,1)) {
			/* Parameter is a number */
			switch(com_num) {
				case CTOA  :
				case ADDSTR: 
				case MAXSTR:
				case MINSTR:
				return ERR_INVALID_ARGUMENT;

				case ADD: val+=atoi(w);  break;
				case SUB: 
				if (cnt==1) val=atoi(w); else val-=atoi(w);  
				break;

				case MUL: 
				if (cnt==1) val=atoi(w); else val*=atoi(w);  
				break;

				case DIV: 
				if (cnt==1) val=atoi(w); else val/=atoi(w);  
				break;

				case MOD: 
				if (cnt==1) val=atoi(w); else val%=atoi(w);  
				break;

				case ATOC:
				sprintf(text,"%c",atoi(w));
				append_string(&result,text);
				break;

				case MAXCOM:
				if (cnt==1) val=atoi(w);
				else if (val<atoi(w)) val=atoi(w);
				break;

				case MINCOM:
				if (cnt==1) val=atoi(w);
				else if (val>atoi(w)) val=atoi(w);
				break;

				case PRINT  : 
				case PRINTNL: 
				if (mesg_q) append_string(&mesg_buff,w);
				else if ((ret=write_stream(w))!=OK) return ret;
				break;
				}
			}
		else {
			/* Parameter is a variable */
			if ((ret=get_variable(w,current_proc))!=OK) {
				FREE(mesg_buff);  return ret;
				}

			valptr=rstack_ptr->value;
			if (com_num>=ADD && 
			    com_num<=MINCOM && 
			    com_num!=CTOA && 
			    !isinteger(valptr,1)) return ERR_INVALID_ARGUMENT;

			switch(com_num) {
				case ADDSTR: 
				append_string(&result,valptr);  break;

				case ADD: val+=atoi(valptr); break;

				case SUB:
				if (cnt==1) val=atoi(valptr); 
				else val-=atoi(valptr);  
				break;

				case MUL:
				if (cnt==1) val=atoi(valptr);
				else val*=atoi(valptr);  
				break;

				case DIV:
				if (cnt==1) val=atoi(valptr);
				else val/=atoi(valptr);  
				break;

				case MOD:
				if (cnt==1) val=atoi(valptr);
				else val%=atoi(valptr);  
				break;

				case ATOC:
				sprintf(text,"%c",atoi(valptr));
				append_string(&result,text);
				break;

				case CTOA:
				if (valptr==NULL) break;
				while(*valptr) {
					sprintf(text,"%d ",*valptr);  
					append_string(&result,text);
					++valptr;
					}
				break;

				case MAXCOM:
				if (cnt==1) val=atoi(valptr);
				else if (val<atoi(valptr)) val=atoi(valptr);
				break;

				case MINCOM:
				if (cnt==1) val=atoi(valptr);
				else if (val>atoi(valptr)) val=atoi(valptr);
				break;

				case MAXSTR:
				if (cnt==1) set_string(&mm_str,valptr);
				else if (compare_strings(mm_str,valptr)<0)
						set_string(&mm_str,valptr);
				break;

				case MINSTR:
				if (cnt==1) set_string(&mm_str,valptr);
				else if (compare_strings(mm_str,valptr)>0)
						set_string(&mm_str,valptr);
				break;

				case PRINT  : 
				case PRINTNL:
				if (valptr!=NULL) {
					if (mesg_q) append_string(&mesg_buff,valptr);
					else if ((ret=write_stream(valptr))!=OK) 
							return ret;
					}
				break;
				}
			}
		++pc2;
		} /* end switch */
	} /* end for */

/*** Set results ****/
switch(com_num) {
	case CTOA:
	/* Get rid of end space */
	if (result!=NULL) result[strlen(result)-1]='\0'; 
	/* Fall through */

	case ATOC:
	case ADDSTR:
	push_rstack(result);  FREE(result);  return OK;

	case MAXSTR:
	case MINSTR:
	push_rstack(mm_str);  FREE(mm_str);  return OK;

	case PRINT: 
	case PRINTNL: 
	set_variable("$print_ok",NULL,"1",1);
	if (mesg_q && mesg_buff!=NULL) {
		if ((ret=write_stream(mesg_buff))!=OK) return ret;
		free(mesg_buff);
		}
	if (com_num==PRINT) return OK;
	return write_stream("\n");
	}
/* Maths commands end up here */
sprintf(text,"%d",val);
return push_rstack(text);
}



/*** Function for the "goto" command ***/
com_goto(pc)
int *pc;
{
struct labels *label;
int ret,tmp;

tmp=*pc+1;
if ((ret=push_rstack_result(&tmp,ALL_LEGAL))!=OK) return ret;

/* Find matching label */
for(label=first_label;label!=NULL;label=label->next) {
	if (label->proc==current_proc && 
	    !strcmp(label->name,rstack_ptr->value)) {
		*pc=label->pos;  return OK;
		}
	}
return ERR_UNDEF_LABELNAME;
}



/*** Function for the "if" command ***/
com_if(pc)
int *pc;
{
int nested,ret;

if ((ret=evaluate_complete_expression(*pc))!=OK) return ret;
if (eval_result) return OK;
nested=1;

/* Find "else" or "endif" if there isn't one */
for(*pc=prog_word[*pc].end_pos+1;*pc<num_words;++*pc) {
	switch(prog_word[*pc].com_num) {
		case IF   : nested++;  continue;
		case ELSE : if (!(nested-1)) {  ++*pc;  return OK;  };  continue;
		case ENDIF: if (!--nested) {  ++*pc;  return OK;  };  continue;
		case ENDPROC:   return ERR_MISSING_ENDIF;
		}
	}
return ERR_MISSING_ENDIF;
}



/*** Function for the "else" command ***/
com_else(pc)
int *pc;
{
int nested;

nested=1;

/* Find endif */
for(*pc=*pc+1;*pc<num_words;++*pc) {
	switch(prog_word[*pc].com_num) {
		case IF     : nested++;  continue;
		case ENDIF  : if (!--nested) {  ++*pc;  return OK;  };  continue;
		case ENDPROC: return ERR_MISSING_ENDIF;
		}
	}
return ERR_MISSING_ENDIF;
}



/*** Function for the "while" command ***/
com_while(pc)
int *pc;
{
struct loops_choose *loop;
int ret;

if ((ret=evaluate_complete_expression(*pc))!=OK) return ret;
if (eval_result) {
	/* Get new loop */
	if (current_loop==NULL || current_loop->start_pos!=*pc) {
		for(loop=first_loop;loop!=NULL;loop=loop->next) {
			if (loop->start_pos==*pc) {
				current_loop=loop;
				return push_lstack(loop);
				}
			}
		return ERR_INTERNAL;
		}
	return OK;
	}

/* Get loop end */
for(loop=first_loop;loop!=NULL;loop=loop->next) {
	if (loop->start_pos==*pc) {
		*pc=loop->end_pos+1;  
		if (current_loop!=loop) return OK;

		pop_lstack();
		if (lstack_ptr!=NULL) current_loop=lstack_ptr->loop;
		else current_loop=NULL;
		return OK;
		}
	}
return ERR_INTERNAL;
}



/*** Function for the "wend" and "next" commands ***/
com_wend_next(pc)
int *pc;
{
struct loops_choose *loop;

if (current_loop==NULL || current_loop->proc!=current_proc) 
	return ERR_UNEXPECTED_COMMAND;
for(loop=first_loop;loop!=NULL;loop=loop->next) {
	if (loop->end_pos==*pc) {
		if (current_loop!=loop) return OK;
		goto FOUND;
		}
	}
return ERR_UNEXPECTED_COMMAND;

FOUND:
*pc=current_loop->start_pos;
return OK;
}



/*** Function for the "do" command ***/
com_do(pc)
int pc;
{
struct loops_choose *loop;

/* Get new loop */
if (current_loop==NULL || current_loop->start_pos!=pc) {
	for(loop=first_loop;loop!=NULL;loop=loop->next) {
		if (loop->start_pos==pc) {
			current_loop=loop;
			return push_lstack(loop);
			}
		}
	return ERR_INTERNAL;
	}
return OK;
}
	


/*** Function for the "until" command ***/
com_until(pc)
int *pc;
{
int ret;

if (current_loop==NULL || current_loop->proc!=current_proc) 
	return ERR_UNEXPECTED_COMMAND;
if ((ret=evaluate_complete_expression(*pc))!=OK) return ret;
if (!eval_result) *pc=current_loop->start_pos+1;
return OK;
}



/*** Function for the "for" command ***/
com_for(pc)
int *pc;
{
struct loops_choose *loop;
int pc2,cnt,from,to,step;
int new,ret,val;
char *w,*valptr[7];

cnt=0;
from=0;
to=0;
step=0;
new=0;
valptr[2]=NULL;
valptr[4]=NULL;
valptr[6]=NULL;

/* Get new loop */
if (current_loop==NULL || current_loop->start_pos!=*pc) {
	for(loop=first_loop;loop!=NULL;loop=loop->next) {
		if (loop->start_pos==*pc) {
			if ((ret=push_lstack(loop))!=OK) return ret;
			current_loop=loop;
			new=1;
			goto FOUND;
			}
		}
	return ERR_INTERNAL;
	}

FOUND:

/* Go through commands parameters */
for(pc2=*pc+1;pc2<=prog_word[*pc].end_pos;) {
	w=prog_word[pc2].word;

	/* We check the 1st , 3rd and 5th position words which should be
	   a variable , "to" and "step" respectively */
	if (++cnt==7) return ERR_SYNTAX;
	switch(cnt) {
		case 1: 
		if ((ret=get_variable(w,current_proc))!=OK) return ret;
		++pc2;  continue;

		case 3:
		if (strcmp(w,"to")) return ERR_SYNTAX;
		++pc2;  continue;

		case 5:
		if (strcmp(w,"step")) return ERR_SYNTAX;
		++pc2;  continue;
		}
	/* Push parameters */
	if ((ret=push_rstack_result(&pc2,STRING_ILLEGAL))!=OK) return ret;
	valptr[cnt]=rstack_ptr->value;
	}	
if (cnt<4 || (cnt>4 && isnull(valptr[6]))) return ERR_SYNTAX;

/* Get final parameter values then set variable and do checks */
from=atoi(valptr[2]);
to=atoi(valptr[4]);
if (valptr[6]!=NULL) step=atoi(valptr[6]);  
else step=(to>from) ? 1 : -1;

/* If just starting loop set var to initial value else get its current value */
w=prog_word[*pc+1].word;
if (new) {
	current_loop->for_to=to;
	if ((ret=set_variable(w,current_proc,valptr[2],0))!=OK)
		return ret;
	val=atoi(valptr[2]);
	}
else {
	if ((ret=get_variable(w,current_proc))!=OK) return ret;

	/* See if var is still a number */
	if (!isinteger(rstack_ptr->value,1)) return ERR_INVALID_ARGUMENT;
	val=atoi(rstack_ptr->value)+step;
	sprintf(text,"%d",val);
	if ((ret=set_variable(w,current_proc,text,0))!=OK) return ret;
	}

/* check */
if ((step>0 && val>to) || (step<0 && val<to)) {
	current_loop->for_to=0;
	*pc=current_loop->end_pos+1;
	pop_lstack();
	if (lstack_ptr==NULL) current_loop=NULL;
	else current_loop=lstack_ptr->loop;
	return OK;
	}
*pc++;
return OK;
}



/*** Function for the "foreach" command ***/
com_foreach(pc)
int *pc;
{
int new,cnt,ret;
struct loops_choose *loop;
struct vars *var;
struct array *arr;
char *w1,*w2,*w4;

if (prog_word[*pc].end_pos - *pc!=4 || 
    strcmp(prog_word[*pc+3].word,"in")) return ERR_SYNTAX;

w1=prog_word[*pc+1].word;
w2=prog_word[*pc+2].word;
w4=prog_word[*pc+4].word;

/* Get new loop */
if (current_loop==NULL || current_loop->start_pos!=*pc) {
	for(loop=first_loop;loop!=NULL;loop=loop->next) {
		if (loop->start_pos==*pc) {
			if ((ret=push_lstack(loop))!=OK) return ret;
			current_loop=loop;
			new=1;
			goto FOUND;
			}
		}
	return ERR_INTERNAL;
	}
else new=0;

FOUND:

/* Get searched array. If we've been through this loop once already 
   foreach_var will already be set */
if (new && current_loop->foreach_var==NULL) {
	if ((ret=get_variable(w4,current_proc))!=OK) return ret;
	var=current_var;
	if (var->refvar!=NULL) var=var->refvar;
	if (!(var->flags & ARRAY)) return ERR_VAR_IS_NOT_ARRAY;
	current_loop->foreach_var=var;
	}
else var=current_loop->foreach_var;

/* Get next array element */
cnt=0;
current_loop->foreach_pos++;
for(arr=var->arr;arr!=NULL;arr=arr->next) {
	if (cnt==current_loop->foreach_pos) break;
	++cnt;
	}

/* No more elements in the array */
if (arr==NULL) {
	current_loop->foreach_pos=-1;
	current_loop->foreach_var=NULL;
	*pc=current_loop->end_pos+1;  
	pop_lstack();
	if (lstack_ptr!=NULL) current_loop=lstack_ptr->loop;
	else current_loop=NULL;
	return OK;
	}

/* Set subscript and value vars */
if ((ret=set_variable(w1,current_proc,arr->subscript,0))!=OK) return ret;
if ((ret=set_variable(w2,current_proc,arr->value,0))!=OK) return ret;
*pc=prog_word[*pc].end_pos+1;
return OK;
}



/*** Function for the "choose" command ***/
com_choose(pc)
int *pc;
{
struct loops_choose *loop;
int pc2,tmp,defpos,endpos,ret,nested;
char *valptr;

/* Find this "loop" */
if (current_loop==NULL || current_loop->start_pos!=*pc) {
	for(loop=first_loop;loop!=NULL;loop=loop->next) {
		if (loop->start_pos==*pc) {
			if ((ret=push_lstack(loop))!=OK) return ret;
			current_loop=loop;
			goto FOUND;
			}
		}
	return ERR_INTERNAL;
	}

FOUND:
/* Get argument value */
tmp=*pc+1;
if ((ret=push_rstack_result(&tmp,ALL_LEGAL))!=OK) return ret;
valptr=rstack_ptr->value;

endpos=prog_word[*pc].end_pos;
defpos=-1;
nested=0;

/* Find appropriate "value" command , or "default" */
for(pc2=endpos+1;pc2<num_words;) {
	switch(prog_word[pc2].com_num) {
		/* Got a nested choose statement , ignore it */
		case CHOOSE:  ++nested;  break;

		case CHOSEN:
		if (!nested) {
			if (defpos!=-1) *pc=defpos; else *pc=pc2;
			return OK;
			}
		--nested;
		break;

		case DEFAULT: 
		if (!nested) defpos=pc2; 
		break;

		case VALUE:
		++pc2;
		if (nested) continue;
		if ((ret=push_rstack_result(&pc2,ALL_LEGAL))!=OK) return ret;
		if (isnull(rstack_ptr->value)) {  
			if (isnull(valptr)) {  *pc=pc2;  return OK;  }
			continue;
			}
		if (valptr && !strcmp(rstack_ptr->value,valptr)) {
			*pc=pc2;  return OK;
			}
		continue;
		} 
	++pc2;
	} 

/* Should never get here */
return ERR_INTERNAL;
}



/*** Function for the "value" & "default" commands ***/
com_val_def()
{
if (current_loop==NULL || 
    current_loop->type!=CHOOSE ||
    current_loop->proc!=current_proc) return ERR_UNEXPECTED_COMMAND;
return OK;
}



/*** Function for "chosen" command ***/
com_chosen(pc)
int *pc;
{
struct loops_choose *loop;

if (current_loop==NULL || current_loop->proc!=current_proc) 
	return ERR_UNEXPECTED_COMMAND;
for (loop=first_loop;loop!=NULL;loop=loop->next) {
	if (loop->end_pos==*pc) {
		if (current_loop!=loop) return OK;
		goto FOUND;
		}
	}
return ERR_UNEXPECTED_COMMAND;

FOUND:
pop_lstack();
if (lstack_ptr==NULL) current_loop=NULL;
else current_loop=lstack_ptr->loop;
(*pc)++;
return OK;
}



/*** Function for "break" command ***/
com_break(pc)
int *pc;
{
if (current_loop==NULL || current_loop->proc!=current_proc) 
	return ERR_UNEXPECTED_COMMAND;

*pc=prog_word[current_loop->end_pos].end_pos+1;
current_loop->foreach_pos=-1;
current_loop->foreach_var=NULL;
pop_lstack();
if (lstack_ptr==NULL) current_loop=NULL; 
else current_loop=lstack_ptr->loop;
return OK;
}



/*** Function for "continue" command ***/
com_continue(pc)
int *pc;
{
if (current_loop==NULL || current_loop->proc!=current_proc) 
	return ERR_UNEXPECTED_COMMAND;
*pc=current_loop->start_pos;
return OK;
}



/*** Function for "call" command. No result returned on rstack as this
     command can't be nested anyway (yet). Results of a proc are returned in 
     $proc:x array element ***/
com_call(pc)
int *pc;
{
struct procedures *proc;
struct proc_stack *stkptr;
struct vars *v1;
int pc1,pc2,endpos1,endpos2,flags;
int ret,new_real_line,pass_by_ref;
char *w,*newvar;

endpos1=prog_word[*pc].end_pos;
if (endpos1 - *pc < 1) return ERR_SYNTAX;
w=prog_word[*pc+1].word;

/* Get proc name */
for(proc=first_proc;proc!=NULL;proc=proc->next)
	if (!strcmp(proc->name,w)) break;
if (proc==NULL) return ERR_UNDEF_PROC;

/* See if already on stack , if so give error */
for (stkptr=pstack_start;stkptr;stkptr=stkptr->next)
	if (stkptr->proc==proc) return ERR_RECURSION;

pc1=*pc+2;
pc2=proc->start_pos+2;  /* Start of receiving parameters variables */
endpos2=prog_word[proc->start_pos].end_pos;
new_real_line=prog_word[proc->start_pos].real_line;

/* Go through parameters of calling "call" command and receiving "proc"
   command and see if they match */
while(1) {
	if (pc1>endpos1 || pc2>endpos2) break;

	/* Work out parameter values */
	w=prog_word[pc1].word;
	newvar=prog_word[pc2].word;
	if (*newvar=='*') {  newvar++;  pass_by_ref=1;  } 
	else pass_by_ref=0;
	if (*newvar=='@') {  newvar++;  flags=ARRAY;  } else flags=0;

	current_var=NULL;
	if ((ret=push_rstack_result(&pc1,ALL_LEGAL))!=OK) return ret;

	/* Create and set parameter variables */
	if (pass_by_ref) {
		/* Reference pass , point to passed variable */
		if (current_var==NULL) return ERR_INVALID_REF_PASS;
		if ((ret=create_variable(newvar,proc,current_var,flags,0))!=OK) {
			real_line=new_real_line;  return ret;
			}
		}
	else {
		/* Value pass */
		v1=current_var;
		if ((ret=create_variable(newvar,proc,NULL,flags,0))!=OK) {
			real_line=new_real_line;  return ret;
			}

		/* If we're passing an array to an array copy it specifically. 
		   This preserves the subscripts as well as the values */
		if ((flags & ARRAY) && v1!=NULL && (v1->flags & ARRAY) &&
		    !strcmp(w,v1->name)) {
			if ((ret=copy_array(v1,current_var))!=OK) {
				real_line=new_real_line;  return ret;
				}
			}
		else if ((ret=set_variable(newvar,proc,rstack_ptr->value,1))!=OK) {
				real_line=new_real_line;  return ret;
				}
		}
	pc2++;
	}
if ((pc1>endpos1 && pc2<=endpos2) ||
    (pc2>endpos2 && pc1<=endpos1)) return ERR_PARAM_COUNT;

/* See if we enable or disable interrupts when calling this procedure */
if (proc->ei_di_int) {
	proc->old_int=current_pcs->int_enabled;
	current_pcs->int_enabled=proc->ei_di_int-1;
	sprintf(text,"%d",current_pcs->int_enabled);
	set_variable("$int_enabled",NULL,text,1);
	}

/* Set up pstack data and pc */
pstack_ptr->ret_pc=endpos1+1;
current_proc=proc;
*pc=endpos2+1;
return push_pstack(proc);
}



/*** Function for "return" and "endproc" commands */
com_return_endproc(com_num,pc) 
int com_num,*pc;
{
int ret,tmp;
char *val;

if (pstack_ptr->prev==NULL) {
	current_proc=NULL;  return OK;
	}

/* Set result. If command is ENDPROC we just set result to whatevers
   on the stack when we exit. */
if (com_num==RETURN && prog_word[*pc].end_pos - *pc > 0) {
	tmp=*pc+1;
	if ((ret=push_rstack_result(&tmp,ALL_LEGAL))!=OK) return ret;
	}

/* Set return variables. */
if (rstack_ptr!=NULL) val=rstack_ptr->value; else val=NULL;
sprintf(text,"$proc:\"%s\"",current_proc->name);
set_variable(text,NULL,val,1);
set_variable("$result",NULL,val,1);

/* Destroy proc vars and reset loops */
destroy_proc_variables(current_proc); 
while(current_loop!=NULL && current_loop->proc==current_proc) {
	current_loop->foreach_pos=-1;
	current_loop->foreach_var=NULL;
	pop_lstack();
	if (lstack_ptr!=NULL) current_loop=lstack_ptr->loop;
	else current_loop=NULL;
	}

/* Reset some interrupt stuff */
if (current_pcs->interrupted) {
	current_pcs->interrupted=0; 
	current_pcs->status=current_pcs->old_status;
	}
if (current_proc->ei_di_int) {
	current_pcs->int_enabled=current_proc->old_int;
	sprintf(text,"%d",current_pcs->int_enabled);
	set_variable("$int_enabled",NULL,text,1);
	}

/* Reset procedure to calling one */
pop_pstack();
*pc=pstack_ptr->ret_pc;
current_proc=pstack_ptr->proc;
return OK;
}



/*** Function for "exit" and "sleep" commands ***/
com_exit_sleep(com_num,pc) 
int com_num,pc;
{
int ret,val;

if (prog_word[pc].end_pos-pc<1) return ERR_SYNTAX;

/* Get number exit code */
++pc;
if ((ret=push_rstack_result(&pc,STRING_ILLEGAL))!=OK) return ret;
if ((val=atoi(rstack_ptr->value))<0) return ERR_INVALID_ARGUMENT;

if (com_num==EXIT) {
	/* Negative value shows process_exit() func that was process exit code
	   not a system one, also subtract 1 so can also tell with a zero */
	current_pcs->exit_code=-val-1; 
	current_pcs->status=EXITING;
	return OK;
	}
/* SLEEP */
current_pcs->status=SLEEPING;
current_pcs->sleep_to=(int)time(0)+val;
return OK;
}



/*** Function for "input" command. All this does is set a flag and see if 
     theres any data on the input buffer waiting for it. ***/
com_input(pc) 
int pc;
{
int ret;

if (current_instream->block && current_pcs->status==INPUT_WAIT) return OK;
if (prog_word[pc].end_pos - pc<1) return ERR_SYNTAX;

/* If theres no flag set then either we have just got to the input command or
   the data is ready for us to read out the buffer. */
if (current_pcs->input_buff==NULL && !current_pcs->eof) {
	current_pcs->status=INPUT_WAIT;  return OK;
	}
current_pcs->eof=0;

/* Set variable and clear buffer */
if ((ret=set_variable(prog_word[pc+current_pcs->input_vnum].word,current_proc,current_pcs->input_buff,0))!=OK) 
	return ret;
free(current_pcs->input_buff);
current_pcs->input_buff=NULL;

/* If we haven't reached the end of the variable list continue waiting */
if (pc+current_pcs->input_vnum!=prog_word[pc].end_pos) {
	current_pcs->status=INPUT_WAIT;  
	current_pcs->input_vnum++;
	return OK;
	}
current_pcs->input_vnum=1;
return OK;
}



/*** Function for commands that just take 1 argument ***/
com_strings1(com_num,pc) 
int pc;
{
int ret,len,val,legal,dup;
char *valptr,*result,*s,*s2,*e,*e2,c,c2;

if (prog_word[pc].end_pos - pc<1) return ERR_SYNTAX;

++pc;
if (com_num==ISNUM) legal=ALL_LEGAL; else legal=INT_ILLEGAL;
if ((ret=push_rstack_result(&pc,legal))!=OK) return ret;
valptr=rstack_ptr->value;
if (com_num!=ISNUM && com_num!=STRLEN && isnull(valptr)) 
	return push_rstack(NULL);

switch(com_num) {
	case ISNUM:
	val=isinteger(valptr,1);
	sprintf(text,"%d",val);
	return push_rstack(text);

	case STRLEN:
	if (valptr!=NULL) len=strlen(valptr); else len=0;
	sprintf(text,"%d",len);
	return push_rstack(text);


	case UPPERSTR:
	case LOWERSTR:
	s=NULL;
	set_string(&s,valptr);
	valptr=s;
	while(*valptr) {
		if (com_num==UPPERSTR) *valptr=toupper(*valptr);	
		else *valptr=tolower(*valptr);
		++valptr;
		}
	push_rstack(s);
	free(s);
	return OK;

	case UNIQUE:
	s=valptr;
	result=NULL;
	while(1) {
		/* Get next element */
		while(*s<33 && *s) ++s; /* Skip initial whitespace */
		if (!*s) break;
		e=s;
		while(*e>32) ++e;

		/* See if we've had it before (up to the point in the string
		   where we are now) */
		s2=valptr;  dup=0;  c=*e;
		while(1) {
			while(*s2<33 && *s2 && s2<s) ++s2;
			if (!*s2 || s2==s) break;
			e2=s2;
			while(*e2>32) ++e2;

			/* See if its the same as the one we're checking */
			*e='\0';  c2=*e2;  *e2='\0';
			if (!strcmp(s,s2)) {  
				dup=1;  *e2=c2;  break;  
				}
			*e2=c2;  *e=c;  s2=e2;
			}
		if (!dup) {
			*e='\0';
			append_string(&result,s);
			append_string(&result," ");	
			}
		*e=c;  s=e;
		}
	push_rstack(result);
	FREE(result);
	return OK;


	case HEAD:
	case TAIL:
	s=valptr;
	while(*s<33 && *s) ++s; 
	if (!*s) return push_rstack(NULL);
	s2=s;
	while(*s2>32) ++s2; /* Find end of first word */
	if (com_num==HEAD) {
		c=*s2;  *s2='\0';  push_rstack(s);  *s2=c;  return OK;
		}
	/* TAIL command comes here */
	while(*s2<33 && *s2) ++s2; /* Find beginning of 2nd word */
	if (!*s2) return push_rstack(NULL);
	return push_rstack(s2);


	case RHEAD:
	case RTAIL:
	s=valptr+strlen(valptr);
	while(*s<33 && s>valptr) --s;  /* Skip end whitespace */
	if (s==valptr) return push_rstack(NULL);
	s2=s;
	while(*s2>32 && s2>valptr) --s2;  /* Find beginning of last word */
	if (com_num==RHEAD) {
		if (s2!=valptr) ++s2;
		++s;  c=*s;  *s='\0';  push_rstack(s2);  *s=c;  return OK;
		}
	while(*s2<33 && s2>valptr) --s2; /* Find end of 2nd last word */
	++s2;  c=*s2;  *s2='\0';  
	push_rstack(valptr);  
	*s2=c;
	return OK;
	}
return ERR_INTERNAL;
}



/*** Function for the mulstr, crypt, match, rename & copy commands. They all 
     take exactly 2 arguments (all strings except for mulstr last arg) ***/
com_strings2(com_num,pc)
int com_num,pc;
{
struct streams *st;
int pc2,cnt,num1,ret,i,legal;
char *valptr[2],*result,pathname1[200],pathname2[200];
char c,c2,*s,*s2,*e,*e2;
FILE *fpold,*fpnew;

if (prog_word[pc].end_pos - pc <2) return ERR_SYNTAX;

/* Go through commands parameters */
cnt=0;
for(pc2=pc+1;pc2<=prog_word[pc].end_pos;) {
	switch(cnt) {
		case 0: 
		if ((ret=push_rstack_result(&pc2,INT_ILLEGAL))!=OK) 
			return ret;
		break;

		case 1:
		if (com_num!=MULSTR) legal=INT_ILLEGAL; 
		else legal=STRING_ILLEGAL;
		if ((ret=push_rstack_result(&pc2,legal))!=OK) 
			return ret;
		break;

		default: return ERR_SYNTAX;
		}	
	valptr[cnt]=rstack_ptr->value;
	cnt++;
	}
if (cnt<2) return ERR_SYNTAX;

if (isnull(valptr[0])) {
	if (com_num==RENAME) return ERR_INVALID_ARGUMENT;
	return push_rstack(NULL);
	}
if (isnull(valptr[1])) {
	if (com_num==MATCH) return push_rstack(NULL);
	return ERR_INVALID_ARGUMENT;
	}
if (com_num==MULSTR && (num1=atoi(valptr[1]))<1) return ERR_INVALID_ARGUMENT;

if (com_num==RENAME || com_num==COPY) {
	/* Make sure another process is not currently accessing either file.
	   If this process is accessing it destroy the stream. */
	for(st=first_stream;st!=NULL;st=st->next) {
		if (st->filename!=NULL && 
		    (!strcmp(st->filename,valptr[0]) || 
		     !strcmp(st->filename,valptr[1]))) {
			if (st->owner!=current_pcs) return ERR_FILE_IN_USE;
			destroy_stream(st); 
			}
		}
	strcpy(pathname1,create_path(valptr[0]));
	strcpy(pathname2,create_path(valptr[1]));
	}
else result=NULL;

/* Do command specific stuff */
switch(com_num) {
	case CRYPT:
	set_string(&result,crypt(valptr[0],valptr[1]));
	break;

	case MULSTR:
	for(i=0;i<num1;++i) append_string(&result,valptr[0]);
	break;

	case MATCH:
	s=valptr[0];
	while(1) {
		/* Find beginning and end of next element in valptr[0] */
		while(*s<33 && *s) ++s;
		if (!*s) break;
		e=s;
		while(*e>32) ++e;
		c=*e;  *e='\0';

		/* Go through valptr[1] and compare elements */
		s2=valptr[1];
		while(1) {
			while(*s2<33 && *s2) ++s2;
			if (!*s2) break;
			e2=s2;
			while(*e2>32) ++e2;
			c2=*e2;  *e2='\0';

			/* compare the 2 elements */
			if (!strcmp(s,s2)) {
				append_string(&result,s);  
				append_string(&result," ");
				*e2=c2;
				break;
				}
			*e2=c2;  s2=e2;
			}
		*e=c;  s=e;
		}
	break;

	case RENAME:
	if (rename(pathname1,pathname2)) {
		sprintf(text,"Process %d (%s) got rename() error: %s",current_pcs->pid,current_pcs->name,sys_errlist[errno]);
		write_syslog(text);
		return ERR_CANT_RENAME_FILE;
		}
	return OK;

	case COPY:
	if (!(fpold=fopen(pathname1,"r"))) return ERR_CANT_OPEN_FILE;
	if (!(fpnew=fopen(pathname2,"w"))) {
		fclose(fpold);  return ERR_CANT_OPEN_FILE;
		}
	c=getc(fpold);
	while(!feof(fpold)) {
		putc(c,fpnew);  c=getc(fpold);
		}
	fclose(fpold);
	fclose(fpnew);
	return OK;
	}
push_rstack(result);
FREE(result);
return OK;
}



/*** Function for substr, instr, midstr, insertstr, overstr, rpadstr, lpadstr, 
     insertelem, overelem, subelem, elements & member commands. They all take 
     3 arguments though for some commands the 3rd argument is optional ***/
com_strings3(com_num,pc) 
int com_num,pc;
{
int cnt,pc2,ret,len,num1,num2;
char c,*w,*res,*valptr[3],*result,*s,*s2;

if (prog_word[pc].end_pos - pc < 2) return ERR_SYNTAX;

cnt=-1;
valptr[0]=NULL;
valptr[1]=NULL;
valptr[2]=NULL;
for(pc2=pc+1;pc2<=prog_word[pc].end_pos;) {
	if (++cnt==3) return ERR_SYNTAX;

	/* Make sure 1st argument and 2nd if command not one of those tested
	   is not a number */
	if ((!cnt || 
	     (cnt==1 && 
	      com_num!=MIDSTR && 
	      com_num!=SUBSTR && 
	      com_num!=SUBELEM && 
	      com_num!=ELEMENTS)) &&
	    isinteger(prog_word[pc2].word,1)) return ERR_INVALID_ARGUMENT;

	if ((ret=push_rstack_result(&pc2,ALL_LEGAL))!=OK) return ret;
	valptr[cnt]=rstack_ptr->value;
	}

/* Make sure we have enough arguments (3rd arg is optional for commands
   tested below) */
if (cnt<2 && 
    com_num!=INSTR && 
    com_num!=MEMBER && 
    com_num!=MIDSTR && 
    com_num!=SUBSTR &&
    com_num!=SUBELEM &&
    com_num!=ELEMENTS) return ERR_SYNTAX;

if (valptr[0]!=NULL) len=strlen(valptr[0]); else len=0;

/* Last element must always be a positive number except for substr etc for
   which a value is optional */
num2=1;
if (isnull(valptr[2])) {
	if (cnt==2 ||
	    com_num==INSERTSTR ||
	    com_num==OVERSTR ||
	    com_num==LPADSTR ||
	    com_num==RPADSTR ||
	    com_num==INSERTELEM ||
	    com_num==OVERELEM) return ERR_INVALID_ARGUMENT;
	}
else if (!isinteger(valptr[2],0) || (num2=atoi(valptr[2]))<1) 
		return ERR_INVALID_ARGUMENT;


/* Do command specific stuff. Remember that strings start at position _1_ in 
   this language and not 0. */
result=NULL;
switch(com_num) {
	case INSTR:
	num2--;
	if (isnull(valptr[0]) || isnull(valptr[1]) || num2>=len) 
		return push_rstack("0");
	w=valptr[0]+num2;
	if ((res=(char *)strstr(w,valptr[1]))==NULL) return push_rstack("0");

	sprintf(text,"%d",(int)(res - valptr[0])+1);
	return push_rstack(text);


	case SUBELEM :
	case ELEMENTS:
	if (isnull(valptr[0])) return push_rstack(NULL);

	if ((num1=atoi(valptr[1]))<1) return ERR_INVALID_ARGUMENT;
	if (valptr[2]==NULL) num2=num1;
	else if (num1>num2) return ERR_INVALID_ARGUMENT;

	s2=s=valptr[0];	
	cnt=0;
	while(*s2) {
		while(*s<33 && *s) ++s; /* Find start of element */
		s2=s;  cnt++;
		while(*s2>32) ++s2; /* Find end of element */
		if (cnt==num1) result=s;
		if (cnt==num2) {
			if (com_num==SUBELEM) {
				/* If num1==1 and num2 is last element then
				   I want to return NULL so check to see if
				   we're at the end */
				if (num1==1) {
					s=s2;
					while(*s<33 && *s) ++s;
					if (!*s) return push_rstack(NULL);
					}

				/* Set result */
				s=result;  result=NULL;  c=*s;  *s='\0';
				set_string(&result,valptr[0]);
				*s=c;
				append_string(&result,s2);
				goto RESULT;
				}
			c=*s2;  *s2='\0';
			push_rstack(result);
			*s2=c;
			return OK;
			}
		s=s2;
		}
	if (result) {
		if (com_num==ELEMENTS) return push_rstack(result);
		if (num1==1) return push_rstack(NULL);
		c=*result; *result='\0';
		push_rstack(valptr[0]);
		*result=c;
		return OK;
		}
	if (com_num==SUBELEM) return push_rstack(valptr[0]);
	return push_rstack(NULL);


	case INSERTELEM:
	if (isnull(valptr[1])) return push_rstack(valptr[0]);
	/* Fall through to OVERELEM */

	case OVERELEM:
	if (isnull(valptr[0])) return push_rstack(NULL);
	/* Fall through to MEMBER */

	case MEMBER:
	if (com_num==MEMBER && (isnull(valptr[0]) || isnull(valptr[1]))) 
		return push_rstack("0");
	s2=s=valptr[0];	
	cnt=0;
	while(*s2) {
		while(*s<33 && *s) ++s; /* Find start of element */
		s2=s;  cnt++;
		while(*s2>32) ++s2; /* Find end of element */

		/* if member we're at in searched string >= start from 
		   value then check it */
		if (cnt>=num2) {
			if (com_num!=MEMBER) {
				c=*s;  *s='\0';
				set_string(&result,valptr[0]);
				*s=c;
				append_string(&result,valptr[1]);
				if (com_num==INSERTELEM) {
					append_string(&result," ");
					append_string(&result,s);	
					}
				else append_string(&result,s2);
				goto RESULT;
				}
			c=*s2;  *s2='\0';
			if (!strcmp(s,valptr[1])) {
				*s2=c;
				sprintf(text,"%d",cnt);
				return push_rstack(text);
				}
			*s2=c;
			}
		s=s2;
		}
	if (com_num==MEMBER) return push_rstack("0");
	set_string(&result,valptr[0]);
	append_string(&result," ");
	append_string(&result,valptr[1]);
	break;


	case SUBSTR:
	if (isnull(valptr[0])) return push_rstack(NULL);

	if ((num1=atoi(valptr[1]))<1) return ERR_INVALID_ARGUMENT;
	if (valptr[2]==NULL) num2=num1;
	else if (num1>num2) return ERR_INVALID_ARGUMENT;

	if ((num1=atoi(valptr[1]))<1 || 
	    (num1>num2 && valptr[2]!=NULL)) return ERR_INVALID_ARGUMENT;
	if (num1>len) return push_rstack(valptr[0]);
	num1--; 

	/* Return string minus section from num1 to num2 */
	c=valptr[0][num1];  valptr[0][num1]='\0';
	set_string(&result,valptr[0]);
	valptr[0][num1]=c;
	if (num2-1<len) append_string(&result,valptr[0]+num2);
	break;

	
	case MIDSTR:
	if (isnull(valptr[0])) return push_rstack(NULL);

	if ((num1=atoi(valptr[1]))<1) return ERR_INVALID_ARGUMENT;
	if (valptr[2]==NULL) num2=num1;
	else if (num1>num2) return ERR_INVALID_ARGUMENT;

	if ((num1=atoi(valptr[1]))<1 || 
	    (num1>num2 && valptr[2]!=NULL)) return ERR_INVALID_ARGUMENT;
	if (num1>len) return push_rstack(NULL);

	num1--; 
	if (num2-1>=len) set_string(&result,valptr[0]+num1);
	else {
		c=valptr[0][num2];  valptr[0][num2]='\0';
		set_string(&result,valptr[0]+num1);
		valptr[0][num2]=c;
		}
	break;


	case INSERTSTR:
	case OVERSTR: 
	if (isnull(valptr[0])) return push_rstack(NULL);
	if (isnull(valptr[1])) {  
		set_string(&result,valptr[0]);  break;
		}

	/* I'm being nice here , could flag an error */
	if (--num2>len) num2=len; 

	/* create output string */	
	c=valptr[0][num2];
	valptr[0][num2]='\0';
	set_string(&result,valptr[0]); 
	valptr[0][num2]=c;
	append_string(&result,valptr[1]);

	if (com_num==INSERTSTR) append_string(&result,valptr[0]+num2);
	else {
		if (strlen(result)<len)
			append_string(&result,valptr[0]+len-(len-strlen(result)));
		}
	break;


	case LPADSTR:
	if (isnull(valptr[1]) || num2<len) {
		set_string(&result,valptr[0]);  break;
		}
	while(result==NULL || strlen(result)<num2-len) 
		append_string(&result,valptr[1]);
	result[num2-len]='\0';
	append_string(&result,valptr[0]);
	break;


	case RPADSTR:
	set_string(&result,valptr[0]);
	if (isnull(valptr[1]) || num2<len) break;
	while(strlen(result)<num2) append_string(&result,valptr[1]);
	result[num2]='\0';
	}
	
/* Push result */
RESULT:
push_rstack(result);  
free(result);
return OK;
}



/*** Function for "count" command which counts the number of elements
     in a string/list ***/
com_count(pc)
int pc;
{
int ret,cnt;
char *s;

if (prog_word[pc].end_pos - pc<1) return ERR_SYNTAX;

pc++;
if ((ret=push_rstack_result(&pc,ALL_LEGAL))!=OK) return ret;
if (isnull(rstack_ptr->value)) return push_rstack("0"); 

/* Go through string and count elements */
cnt=0;
s=rstack_ptr->value;
while(1) {
	/* Find beginning of element */
	while(*s<33 && *s) ++s;
	if (!*s) break;
	cnt++;

	/* Find end of element */
	while(*s>33) ++s;
	}
sprintf(text,"%d",cnt);
return push_rstack(text);
}



/*** Function for "arrsize" ***/
com_arrsize(pc)
int pc;
{
struct vars *var;
struct array *arr;
int ret,cnt;
char *w;

if (prog_word[pc].end_pos - pc<1) return ERR_SYNTAX;

w=prog_word[pc+1].word;

/* Check for errors */
if (*w=='[' || *w=='"' || isinteger(w,1)) 
	return ERR_INVALID_ARGUMENT;
if (strstr(w,":") || strstr(w,"#") || strstr(w,"?"))
	return ERR_VAR_IS_NOT_ARRAY;

/* find variable */
if ((ret=get_variable(w,current_proc))!=OK) return ret;
var=current_var;
if (var->refvar!=NULL) var=var->refvar;
if (!(var->flags & ARRAY)) return ERR_VAR_IS_NOT_ARRAY;
	
/* Got the array , now count it */
cnt=0;
for(arr=current_var->arr;arr!=NULL;arr=arr->next) ++cnt;
sprintf(text,"%d",cnt);  
return push_rstack(text);
}



/*** Function for "trap" command ***/
com_trap(pc)
int pc;
{
int pc2,ret;
char *w;

w=prog_word[pc+1].word;
if (*w!='[') return ERR_INVALID_ARGUMENT;
pc2=pc+2;
ret=exec_command(&pc2);
sprintf(text,"%d",ret);
if ((ret=set_variable("$last_error",NULL,text,1))!=OK) return ret;
return push_rstack(text);
}



/*** Function for the in, out, block and nonblock stream commands. Blocking
     only affects input streams. ***/
com_streams(com_num,pc)
int com_num,pc;
{
struct streams *st;
int ret;

if (prog_word[pc].end_pos-pc<1) return ERR_SYNTAX;

++pc;
if ((ret=push_rstack_result(&pc,ALL_LEGAL))!=OK) return ret;
if (isnull(rstack_ptr->value)) return ERR_INVALID_ARGUMENT;

switch(com_num) {
	case IN:
	case OUT:
	return set_iostreams((com_num==IN ? INPT : OUTPT),rstack_ptr->value);


	case BLOCK:
	case NONBLOCK:
	if ((st=get_stream(rstack_ptr->value))==NULL) 
		return ERR_INVALID_STREAM;
	if (st->owner!=current_pcs) return ERR_INVALID_STREAM;

	sprintf(text,"%d",st->block); /* return what it was before */
	st->block=((com_num==BLOCK) ? 1 : 0);
	return push_rstack(text);


	case LOCK:
	case UNLOCK:
	if ((st=get_stream(rstack_ptr->value))==NULL) 
		return ERR_INVALID_STREAM;
	if (st->owner!=current_pcs || st->internal==-1) 
		return ERR_INVALID_STREAM;
	
	sprintf(text,"%d",st->locked); /* return what it was before */
	st->locked=((com_num==LOCK) ? 1 : 0);
	return push_rstack(text);
	}
return ERR_INTERNAL;
}



/*** Function for open file commands ***/
com_open(pc)
int pc;
{
int flags,rw,ret,fd,com;
char *file,pathname[ARR_SIZE];
char *sub_com[]={ "read","write","append","readwrite" };
struct streams *st;

if (prog_word[pc].end_pos - pc < 3 || strcmp(prog_word[pc+1].word,"to")) 
	return ERR_SYNTAX;

/* Get access type */
pc+=2;
for(com=0;com<4;++com) 
	if (!strcmp(sub_com[com],prog_word[pc].word)) break;
switch(com) {
	case 0 : rw=READ;       flags=O_RDONLY;                       break;
	case 1 : rw=WRITE;      flags=O_WRONLY | O_CREAT;             break;
	case 2 : rw=APPEND;     flags=O_WRONLY | O_APPEND | O_CREAT;  break;
	case 3 : rw=READWRITE;  flags=O_RDWR | O_CREAT;               break;

	default: return ERR_SYNTAX;
	}

pc++;
if ((ret=push_rstack_result(&pc,INT_ILLEGAL))!=OK) return ret;

/* Do some checks first */
if (isnull(file=rstack_ptr->value)) return ERR_INVALID_ARGUMENT;

/* If we're trying to write to a file (which requires deleting it first)
   check that this or any other process is not accessing it already */
if (rw==WRITE) {
	for(st=first_stream;st!=NULL;st=st->next) {
		if (st->filename!=NULL && !strcmp(st->filename,file))
			return ERR_FILE_IN_USE;
		}
	}
strcpy(pathname,create_path(file));

/* If we are WRITEing then delete output first as open() won't do this for us 
   (unlike fopen) if the file already exists and we'll simply end up 
   overwriting the current data from the start. If you *do* want to overwrite
   the data use READWRITE. ***/
if (rw==WRITE) remove(pathname); 

/* Open file and set stream */
if ((fd=open(pathname,flags,0644))==-1) {
	sprintf(text,"Process %d (%s) got open() error: %s",current_pcs->pid,current_pcs->name,sys_errlist[errno]);
	write_syslog(text);
	return ERR_CANT_OPEN_FILE;
	}
sprintf(text,"FILE_%s",file);
if ((ret=create_stream(text,file,fd,-1,rw,current_pcs))!=OK) {
	close(fd);  return ret;
	}

/* Set eof var and return stream name as result */
set_variable("$eof",NULL,"0",1);
return push_rstack(text);
}



/*** Function for close stream & delete file commands ***/
com_close_delete(com_num,pc)
int com_num,pc;
{
struct streams *st;
char *valptr,pathname[200];
int ret,end;

end=prog_word[pc].end_pos;
if (end - pc < 1) return ERR_SYNTAX; 

/* loop through arguments */
for(pc=pc+1;pc<=end;) {
	if ((ret=push_rstack_result(&pc,INT_ILLEGAL))!=OK) return ret;
	if (isnull(valptr=rstack_ptr->value)) return ERR_INVALID_ARGUMENT;
	if (com_num==CLOSE) {
		if ((st=get_stream(valptr))==NULL) return ERR_INVALID_STREAM;
		destroy_stream(st);
		continue;
		}

	/* DELETE command. Make sure another process is not currently 
	   accessing the file. If this process is accessing it destroy the 
	   stream. */
	for(st=first_stream;st!=NULL;st=st->next) {
		if (st->filename!=NULL && !strcmp(st->filename,valptr)) {
			if (st->owner!=current_pcs) return ERR_FILE_IN_USE;
			destroy_stream(st); 
			}
		}

	strcpy(pathname,create_path(valptr));
	if (remove(pathname)) {
		sprintf(text,"Process %d (%s) got remove() error: %s",current_pcs->pid,current_pcs->name,sys_errlist[errno]);
		write_syslog(text);
		return ERR_CANT_DELETE_FILE;
		}
	}
return OK;
}



/*** Function for seek command. First argument is either "start" or "current"
     so we can seek from start of the file or current position. Remember this
     only works on the input stream , can't seek on output. ***/
com_seek(com_num,pc)
int com_num,pc;
{
int ret,whence;

if (prog_word[pc].end_pos - pc < 2) return ERR_SYNTAX;

if (!strcmp(prog_word[pc+1].word,"start")) whence=SEEK_SET;
else if (!strcmp(prog_word[pc+1].word,"current")) whence=SEEK_CUR;
	else return ERR_SYNTAX;
pc+=2;
if ((ret=push_rstack_result(&pc,STRING_ILLEGAL))!=OK) return ret;
return seek_stream(com_num,whence,atoi(rstack_ptr->value));
}



/*** Return a listing of the given directory ***/
com_dir(pc)
int pc;
{
DIR *dir;
struct dirent *ds;
struct stat fs;
char *sub_com[]={ "all","files","dirs","links","cdevs","bdevs","usocks" };
char *list,pathname[ARR_SIZE],filename[ARR_SIZE];
int com,ret,type;

if (prog_word[pc].end_pos - pc<2) return ERR_SYNTAX;
++pc;
for(com=0;com<7;++com) 
	if (!strcmp(prog_word[pc].word,sub_com[com])) goto FOUND;
return ERR_SYNTAX;

FOUND:
++pc;
if ((ret=push_rstack_result(&pc,INT_ILLEGAL))!=OK) return ret;
if (isnull(rstack_ptr->value)) return ERR_INVALID_ARGUMENT;

strcpy(pathname,create_path(rstack_ptr->value));
if ((dir=opendir(pathname))==NULL) {
	sprintf(text,"Process %d (%s) got opendir() error: %s",current_pcs->pid,current_pcs->name,sys_errlist[errno]);
	write_syslog(text);
	return ERR_CANT_OPEN_DIR;
	}

list=NULL;
while((ds=readdir(dir))!=NULL) {
	sprintf(filename,"%s/%s",pathname,ds->d_name);
	lstat(filename,&fs);
	type=fs.st_mode & S_IFMT;
	switch(com) {
		case 1: if (type==S_IFREG) break; else continue;
		case 2: if (type==S_IFDIR) break; else continue;
		case 3: if (type==S_IFLNK) break; else continue;
		case 4: if (type==S_IFCHR) break; else continue;
		case 5: if (type==S_IFBLK) break; else continue;
		case 6: if (type==S_IFSOCK) break; else continue;
		}
	append_string(&list,ds->d_name);
	append_string(&list," ");
	}
if (list) list[strlen(list)-1]='\0'; /* Remove trailing space */
closedir(dir);
push_rstack(list);
FREE(list);
return OK;
}



/*** Function for the format command which can convert \ escape sequences 
     (except for octal codes) and printf % format patterns. ***/
com_format(pc)
int pc;
{
int ret,pc2,endpos;
char *fptr_start,*fptr_end,*val,*s,*result,c,c2;

endpos=prog_word[pc].end_pos;
if (endpos - pc < 1) return ERR_SYNTAX;

/* Get format string first */
++pc;
if ((ret=push_rstack_result(&pc,INT_ILLEGAL))!=OK) return ret;
if (isnull(fptr_start=rstack_ptr->value)) {
	push_rstack(NULL);  return OK;
	}
fptr_end=fptr_start;
result=NULL;

/* Got format string , now go through arguments */
for(pc2=pc;pc2<=endpos;) {

	/* Go through format till we hit a '%' format pattern or the end. */
	while(*fptr_end) {
		if (*fptr_end=='%') {
			if (*(fptr_end+1)=='%') strcpy(fptr_end+1,fptr_end+2);
			else break;
			}
		++fptr_end;
		}
	if (!(c=*fptr_end)) break;	
	*fptr_end='\0'; 
	append_string(&result,fptr_start); /* Append up to just before '%' */
	*fptr_end=c;
	fptr_start=fptr_end; /* fptr_start now pointing at '%' */

	/* Find end of '%' pattern */
	while(*fptr_end) {
		if (*fptr_end<33)  break;
		if (isalpha(*fptr_end)) {  ++fptr_end;  break;  }
		++fptr_end;
		}
	c=*fptr_end;  *fptr_end='\0';

	/* Get individual argument */
	if ((ret=push_rstack_result(&pc2,ALL_LEGAL))!=OK) return ret;

	/* Got value , now let sprintf work out format (thought %* formats are
	   not supported) and append its output to result. Using the 'text'
	   array here limits the size of the string this can produce. If its
	   too long the program will crash. Tough shit. */
	val=rstack_ptr->value;
	if (val!=NULL && *(fptr_start+1)!='*') {
		c2=*(fptr_end-1);
		if (strchr("c",c2)) {
			if (isinteger(val,1)) sprintf(text,fptr_start,atoi(val));
			else sprintf(text,fptr_start,val[0]);
			}
		else {
			if (strchr("diouxXeE",c2)) 
				sprintf(text,fptr_start,atoi(val));
			else if (strchr("fgG",c2)) 
					sprintf(text,fptr_start,(float)atof(val));
				else sprintf(text,fptr_start,val);
			}
		append_string(&result,text);
		}
	*fptr_end=c;
	fptr_start=fptr_end;
	} /* End for */

append_string(&result,fptr_start);

/* Convert any '\' escape characters in the result string before we push it
   onto the result stack. Note: Octal sequences are not supported, too much 
   hassle to parse. */
s=result;
while(*s) {
	if (*s=='\\' && *(s+1) && *(s+1)!='\\') {
		switch(*(s+1)) {
			/* The \a alert char. should be standard but isn't
			   always supported I've found. */
			case 'a': c='\07'; break;
			case 'b': c='\b';  break;
			case 'f': c='\f';  break; /* form feed (apparently) */
			case 'n': c='\n';  break;
			case 'r': c='\r';  break;
			case 't': c='\t';  break;
			case 'v': c='\v';  break; /* A vertical tab (they say)*/
			case '"': c='"' ;  break;
			default : ++s;  continue;
			}
		/* Set char and shift whole string down by 1 */
		*s=c;
		strcpy(s+1,s+2);
		}
	++s;
	}
push_rstack(result);
free(result);
return OK;
}



/*** Function for "spawn" command ***/
com_spawn(pc)
int pc;
{
int ret,child,len;
char pid[10],*var,*name;
char *type[]={ "orphan","child" };
struct process *cur;

if ((len=prog_word[pc].end_pos - pc)<2) return ERR_SYNTAX;

if (!strcmp(prog_word[pc+1].word,"orphan")) child=0;
else if (!strcmp(prog_word[pc+1].word,"child")) child=1;
	else return ERR_SYNTAX;

/* Check pid var exists */
var=prog_word[pc+2].word;
if ((ret=get_variable(var,current_proc))!=OK) return ret;

/* See if we have an optional process name to give child */
if (len>2) {
	pc+=3;
	if ((ret=push_rstack_result(&pc,INT_ILLEGAL))!=OK) return ret;
	name=rstack_ptr->value;
	}
else name=current_pcs->name;

/* create process */
if ((ret=spawn_process(current_pcs,child,1))!=OK) return ret;

sprintf(text,"Process %d (%s) spawned %s process %d (%s).",current_pcs->pid,current_pcs->name,type[child],last_pcs->pid,name);
write_syslog(text);
	
/* Parent process variable gets set to "child" pid , child variable
   gets set to zero. Also set process name if different. */
cur=current_pcs;
load_process_state(last_pcs);
set_variable(var,current_proc,"0",0);
if (name!=current_pcs->name) {
	set_string(&current_pcs->name,name);
	create_variable("$name",NULL,NULL,READONLY,1);
	set_variable("$name",NULL,current_pcs->name,1);
	}
save_process_state(current_pcs,0);
load_process_state(cur);
sprintf(pid,"%d",last_pcs->pid);
return set_variable(var,current_proc,pid,0);
}



/*** Wait for any child process to exit ***/
com_wait(pc)
int pc;
{
int ret;

if (prog_word[pc].end_pos - pc !=1) return ERR_SYNTAX;

/* See if result var exists */
if ((ret=get_variable(prog_word[pc+1].word,current_proc))!=OK) return ret;
current_pcs->wait_var_proc=current_var->proc;
current_pcs->wait_var=NULL;
set_string(&current_pcs->wait_var,prog_word[pc+1].word);
current_pcs->status=CHILD_DWAIT;
return OK;
}



/*** Wait for a specific process to exit. If we wait on ourself then we'll 
     hang forever. ***/
com_waitpid(pc)
int pc;
{
int ret,pid;

if (prog_word[pc].end_pos - pc < 1) return ERR_SYNTAX;

/* Get argument */
++pc;
if ((ret=push_rstack_result(&pc,STRING_ILLEGAL))!=OK) return ret;
if ((pid=atoi(rstack_ptr->value))<2) return ERR_INVALID_ARGUMENT;

/* See if process exists */
if (get_process(pid)) {
	current_pcs->wait_pid=pid; 
	current_pcs->status=SPEC_DWAIT;
	}
/* Could flag an error if theres no such process but since this command can't
   be nested (and therefor trapped) this would be a nuisance so just return 
   OK */
return OK;
}	



/*** Function for "exists" , "pcsinfo", "relation" & "kill" ***/
com_eprk(com_num,pc)
int com_num,pc;
{
struct process *pcs,*pcs2,*par;
char ppid[10];
int ret,cnt,end;

end=prog_word[pc].end_pos;
if (end - pc < 1) return ERR_SYNTAX;

/* Get argument */
++pc;
if ((ret=push_rstack_result(&pc,STRING_ILLEGAL))!=OK) return ret;
pcs=get_process(atoi(rstack_ptr->value));
if (com_num!=EXISTS && pcs==NULL) return ERR_NO_SUCH_PROCESS;

switch(com_num) {
	case EXISTS:
	if (pcs) return push_rstack("1");
	return push_rstack("0");

	case PCSINFO:
	/* Some potentially usefull info about a process */
	if (pcs->parent==NULL) strcpy(ppid,"0");
	else sprintf(ppid,"%d",pcs->parent->pid);

	sprintf(text,"%s %s %s %d %d %s %d",pcs->name,pcs->filename,ppid,(int)pcs->created,pcs->local_port,pcs->site,pcs->remote_port);
	sprintf(text,"%s %s %d %d %d %d",text,status_name[pcs->status],pcs->int_enabled,pcs->interrupted,pcs->mesg_cnt,pcs->colour);
	return push_rstack(text);

	case KILL:
	if (pcs==first_pcs || pcs->status==IMAGE) return ERR_CANT_KILL;

	/* If kill_any = 0 see if process is a descendent, can't kill it if 
	   not */
	if (!kill_any) {
		for(par=pcs->parent;par && par!=current_pcs;par=par->parent);
		if (par==NULL) return ERR_CANT_KILL;
		}	
	
	/* Return what it was doing just before we killed it */
	push_rstack(status_name[pcs->status]);
	pcs->status=EXITING;
	sprintf(text,"Process %d (%s) killed process %d (%s).",current_pcs->pid,current_pcs->name,pcs->pid,pcs->name);
	write_syslog(text);
	return OK;	

	case RELATION:
	/* get 2nd process */
	if (pc>end) return ERR_SYNTAX;
	if ((ret=push_rstack_result(&pc,STRING_ILLEGAL))!=OK) return ret;
	if ((pcs2=get_process(atoi(rstack_ptr->value)))==NULL)
		return ERR_NO_SUCH_PROCESS;

	/* See if process 1 is an ancestor of 2 */
	for(par=pcs2->parent,cnt=1;par!=NULL;par=par->parent,++cnt) {
		if (par==pcs) {
			sprintf(text,"%d",-cnt);  return push_rstack(text); 
			}
		}

	/* See if process 1 is a descendent */
	for(par=pcs->parent,cnt=1;par!=NULL;par=par->parent,++cnt) {
		if (par==pcs2) {
			sprintf(text,"%d",cnt);  return push_rstack(text); 
			}
		}

	/* Its neither */
	return push_rstack("0");  
	}
return ERR_INTERNAL;
}



/*** Exec another process by loading the program file ***/
com_exec(pc)
int pc;
{
struct process *cur;
int i,pc2,argc,new_pid,ret,old_real_line,cnt;
int in,out,child;
char *argv[MAX_ARGC+1],pid[10],pid2[20];
char *var,*filename,*ptr,*ptr2,c;

if (prog_word[pc].end_pos - pc<3) return ERR_SYNTAX;
if (!strcmp(prog_word[pc+1].word,"child")) child=1;
else if (!strcmp(prog_word[pc+1].word,"orphan")) child=0;
	else return ERR_SYNTAX;

/* Check var exists */
current_var=NULL;
var=prog_word[pc+2].word;
if ((ret=get_variable(var,current_proc))!=OK) return ret;

/* Get new pid */
if ((new_pid=get_new_pid())==-1) return ERR_MAX_PROCESSES;
sprintf(pid,"%d",new_pid);

/* Get arguments */
cnt=0;
argc=0;
filename=NULL;
for(pc2=pc+3;pc2<=prog_word[pc].end_pos;) {
	if ((ret=push_rstack_result(&pc2,ALL_LEGAL))!=OK) return ret;
	if (isnull(ptr=rstack_ptr->value))  break;

	/* Arguments string , break string up into args. Argv[0] will be
	   process name. */
	while(1) {
		while(*ptr<33 && *ptr) ++ptr;
		if (!*ptr) break;
		ptr2=ptr;
		while(*ptr2>32) ++ptr2;
		c=*ptr2;  *ptr2='\0';

		if (++cnt==1) {
			filename=NULL;  set_string(&filename,ptr);
			}
		else {
			if ((argv[argc]=(char *)malloc((int)(ptr2-ptr)+1))==NULL) {
				ret=ERR_MALLOC;  goto FREEARGS;
				}
			strcpy(argv[argc],ptr);
			if (++argc>MAX_ARGC) break;
			}
		ptr=ptr2;  *ptr2=c;
		if (!*ptr2) break;
		}
	}
if (!argc) {  ret=ERR_SYNTAX;  goto FREEARGS;  } 

/* Get our io stream file descriptors */
in=get_stream("STDIN")->external;
out=get_stream("STDOUT")->external;

cur=current_pcs;
save_process_state(cur,0);

/* Set system process array entry. */
load_process_state(first_pcs);
sprintf(pid2,"$pcs:\"%d\"",new_pid); 
set_variable(pid2,NULL,argv[0],1);
save_process_state(first_pcs,0);

/* Create process */
if ((ret=create_process(argv[0],new_pid))!=OK)  {
	load_process_state(cur);  goto FREEARGS;
	}
current_pcs->status=RUNNING;
current_pcs->filename=filename;
if (child) current_pcs->parent=cur; 
else current_pcs->parent=NULL;

set_string(&current_pcs->site,cur->site);
current_pcs->local_port=cur->local_port;
current_pcs->remote_port=cur->remote_port;

/* Load program file */
old_real_line=real_line;
real_line=-1;
if ((ret=process_setup(filename,argc,argv,0))!=OK) {
	destroy_process(current_pcs);
	load_process_state(cur);
	real_line=old_real_line;
	goto FREEARGS;
	}
current_pcs->pc=current_proc->start_pos;

/* Set STDIN, OUT & ERR to the same as exec'ing process */
get_stream("STDIN")->external=in;
get_stream("STDOUT")->external=out;

save_process_state(current_pcs,1);
load_process_state(cur);
real_line=old_real_line;

/* Set pid var */
set_variable(var,current_proc,pid,0);

sprintf(text,"Process %d (%s) executed file '%s' creating process %d (%s).",cur->pid,cur->name,filename,new_pid,argv[0]);
write_syslog(text);
ret=OK;

FREEARGS:
for(i=0;i<argc;++i) free(argv[i]);
push_rstack(pid);
return ret;
}



/*** Set the procedure to jump to on various types of interrupts ***/
com_onint(pc)
int pc;
{
struct procedures *proc;
int len,type;
char *w;

len=prog_word[pc].end_pos-pc;
if (len<2) return ERR_SYNTAX;
if (!strcmp(prog_word[pc+1].word,"from")) {
	if (len<3) return ERR_SYNTAX;
	}
else if (!strcmp(prog_word[pc+1].word,"ignore")) {
		if (len!=2) return ERR_SYNTAX;
		}
	else return ERR_SYNTAX;

w=prog_word[pc+2].word;
if (!strcmp(w,"child")) type=CHILD_INT;
else if (!strcmp(w,"nonchild")) type=NONCHILD_INT;
	else if (!strcmp(w,"timer")) type=TIMER_INT;
		else return ERR_SYNTAX;

switch(len) {
	case 2:
	/* Unset interrupt procedure */
	switch(type) {
		case CHILD_INT: 
		current_pcs->c_int_proc=NULL;  break;

		case NONCHILD_INT: 
		current_pcs->n_int_proc=NULL;  break;

		case TIMER_INT: 
		current_pcs->t_int_proc=NULL;  
		}
	return OK;

	case 3:
	/* Set it */
	for(proc=first_proc;proc!=NULL;proc=proc->next) {
		if (!strcmp(proc->name,prog_word[pc+3].word)) {
			/* Make sure no arguments with procedure */
			if (prog_word[proc->start_pos].end_pos - proc->start_pos!=1)
				return ERR_INT_PROC_ARGS;
			switch(type) {
				case CHILD_INT: 
				current_pcs->c_int_proc=proc;  break;

				case NONCHILD_INT: 
				current_pcs->n_int_proc=proc;  break;

				case TIMER_INT: 
				current_pcs->t_int_proc=proc;
				}
			return OK;
			}
		}
	return ERR_UNDEF_PROC;
	}
return ERR_SYNTAX;
}



/*** Interrupt another process ***/
com_interrupt(pc)
int pc;
{
struct process *ipcs,*cur;
int pid,endpos,ret,int_type;
char *tmp;

endpos=prog_word[pc].end_pos;
if (endpos-pc<3) return ERR_SYNTAX;

/* Get process */
++pc;
if ((ret=push_rstack_result(&pc,STRING_ILLEGAL))!=OK) return ret;
if (pc>endpos) return ERR_SYNTAX;
if ((pid=atoi(rstack_ptr->value))<2) return ERR_INVALID_ARGUMENT;
if ((ipcs=get_process(pid))==NULL) 
	return ERR_NO_SUCH_PROCESS;

if (strcmp(prog_word[pc].word,"with")) return ERR_SYNTAX;

/* Get message */
if (++pc>endpos) return ERR_SYNTAX;
if ((ret=push_rstack_result(&pc,ALL_LEGAL))!=OK) return ret;

if (pc<=endpos) {
	/* See if its an on death interrupt */
	if (!strcmp(prog_word[pc].word,"on") &&
	    !strcmp(prog_word[pc+1].word,"death")) {
		/* Can't interrupt self on death */
		if (ipcs==current_pcs) return ERR_INVALID_ARGUMENT; 

		/* Set process to interrupt and message */
		current_pcs->death_pid=pid;
		set_string(&current_pcs->death_mesg,rstack_ptr->value);

		return push_rstack("1");
		}
	return ERR_SYNTAX;
	}

/* See if we can interrupt it */
if (!ipcs->int_enabled ||
    ipcs->interrupted ||
    ipcs->status==CHILD_INT || 
    ipcs->status==NONCHILD_INT ||
    ipcs->status==TIMER_INT ||
    ipcs->status==IMAGE) return push_rstack("0");

/* See if we're a child of this process */
if (current_pcs->parent==ipcs) int_type=CHILD_INT;
else int_type=NONCHILD_INT;

/* Set other processes vars. Use tmp instead of just text here in case we
   have a very long message */
tmp=NULL;
sprintf(text,"%d ",current_pcs->pid);
set_string(&tmp,text);
append_string(&tmp,rstack_ptr->value);

cur=current_pcs;
save_process_state(cur,0);
load_process_state(ipcs);
set_variable("$int_mesg",NULL,tmp,1);
ipcs->old_status=ipcs->status;
ipcs->status=int_type;
save_process_state(ipcs,0);
load_process_state(cur);
free(tmp);

return push_rstack("1");
}



/*** Set the timer interrupt ****/
com_timer(pc)
int pc;
{
int ret,cnt;

if (prog_word[pc].end_pos - pc < 1) return ERR_SYNTAX;

++pc;
if ((ret=push_rstack_result(&pc,STRING_ILLEGAL))!=OK) return ret;
if ((cnt=atoi(rstack_ptr->value))<0) return ERR_INVALID_ARGUMENT;
current_pcs->timer_exp=(int)time(0)+cnt;
sprintf(text,"%d",current_pcs->timer_exp);
return push_rstack(text);
}



/*** Enable or disable all interrupts ***/
com_ei_di(com_num,pc)
int com_num,pc;
{
struct procedures *proc;
int which;

/* Enable or disable ints now */
if (!(prog_word[pc].end_pos-pc)) {
	current_pcs->int_enabled=(com_num==EI ? 1 : 0);
	sprintf(text,"%d",current_pcs->int_enabled);
	set_variable("$int_enabled",NULL,text,1);
	return push_rstack(text);
	}

/* Enable or disable on a procedure call */
if (!strcmp(prog_word[pc+1].word,"on")) which=1;
else if (!strcmp(prog_word[pc+1].word,"ignore")) which=0;
	else return ERR_SYNTAX;
for(proc=first_proc;proc!=NULL;proc=proc->next) {
	if (!strcmp(proc->name,prog_word[pc+2].word)) {
		proc->ei_di_int=which*(com_num==EI ? 2 : 1);
		sprintf(text,"%d",proc->ei_di_int-1);
		return push_rstack(text);
		}
	}
return ERR_UNDEF_PROC;
}



/*** Get the time and date in various ways ***/
com_gettime(pc)
int pc;
{
struct tm *tms; 
time_t tm_num;
int com,ret;

char *sub_com[]={ 
"sysboot","created","rawtime","date","usdate","revdate","time",
"hour","mins","secs","wday","mday","month","year","dayname","monthname" 
};
char *days[]={
"sunday","monday","tuesday","wednesday","thursday","friday","saturday"
};
char *months[]={
"january","february","march","april","may","june",
"july","august","september","november","december"
};

if (prog_word[pc].end_pos - pc<1) return ERR_SYNTAX;
for(com=0;com<16;++com) 
	if (!strcmp(sub_com[com],prog_word[pc+1].word)) goto FOUND;
return ERR_SYNTAX;

FOUND:
/* If we have a time value given use that else use current system time */
if (prog_word[pc].end_pos - pc>=2) {
	pc+=2;
	if ((ret=push_rstack_result(&pc,STRING_ILLEGAL))!=OK) return ret;
	tm_num=(time_t)atoi(rstack_ptr->value);
	}
else time(&tm_num);

/* Set up the structure */
tms=localtime(&tm_num);

switch(com) {
	case 0: sprintf(text,"%d",(int)sysboot);  break;
	case 1: sprintf(text,"%d",(int)current_pcs->created);  break;
	case 2: sprintf(text,"%d",(int)time(0));  break;

	case 3:
	sprintf(text,"%02d/%02d/%02d",tms->tm_mday,tms->tm_mon+1,tms->tm_year);
	break;

	case 4:
	sprintf(text,"%02d/%02d/%02d",tms->tm_mon+1,tms->tm_mday,tms->tm_year);
	break;

	case 5:
	sprintf(text,"%02d/%02d/%02d",tms->tm_year,tms->tm_mon+1,tms->tm_mday);
	break;

	case 6: 
	sprintf(text,"%02d:%02d:%02d",tms->tm_hour,tms->tm_min,tms->tm_sec);
	break;

	case 7: sprintf(text,"%d",tms->tm_hour);  break;
	case 8: sprintf(text,"%d",tms->tm_min);  break;
	case 9: sprintf(text,"%d",tms->tm_sec);  break;
	case 10: sprintf(text,"%d",tms->tm_wday+1);  break;
	case 11: sprintf(text,"%d",tms->tm_mday);  break;
	case 12: sprintf(text,"%d",tms->tm_mon+1);  break;
	case 13: sprintf(text,"%d",1900+tms->tm_year);  break;
	case 14: sprintf(text,"%s",days[tms->tm_wday]);  break;
	case 15: sprintf(text,"%s",months[tms->tm_mon]);  break;
	}
return push_rstack(text);
}



/*** Set colour for the process on or off ***/
com_colour(pc)
int pc;
{
int com;
char *w,val[2];

w=prog_word[pc+1].word;
if (prog_word[pc].end_pos - pc!=1) return ERR_SYNTAX;
if (!strcmp(w,"off")) com=0;
	else if (!strcmp(w,"on")) com=1;
		else if (!strcmp(w,"val")) com=2;
			else return ERR_SYNTAX;
if (com!=2) current_pcs->colour=com;
sprintf(val,"%d",current_pcs->colour);
return push_rstack(val);
}



/*** This is used by com_connect below ***/
void conalarm()
{
/* Close the socket here because in SunOS the connect() function is auto 
   re-entrant and so I have to force it to exit by doing this. If it wasn't 
   done SunOS would simply go back into connect() and continue waiting. */
close(consock);
conalarm_called=1;
}



/*** Open a socket to a remote address. Due to the blocking connect call
     this can cause the whole system to hang. I could do a non-blocking
     connect call but I wouldn't be able to have to connect command nested
     then and I would have to write code to continually check the socket 
     status. ***/
com_connect(pc)
int pc;
{
struct sockaddr_in con_addr;
struct hostent *hp;
int ret,pc2,port;
char *addr,*s;

if (prog_word[pc].end_pos - pc<2) return ERR_SYNTAX;
pc2=pc+1;

/* Get IP address */
if ((ret=push_rstack_result(&pc2,ALL_LEGAL))!=OK) return ret;
if (pc2>prog_word[pc].end_pos) return ERR_SYNTAX;
addr=rstack_ptr->value;

/* Get port */
if ((ret=push_rstack_result(&pc2,STRING_ILLEGAL))!=OK) return ret;
if ((port=atoi(rstack_ptr->value))<1 || port>65535) return ERR_INVALID_ARGUMENT;

/* See if number IP address given */
s=addr;
while(*s && (*s=='.' || isdigit(*s))) ++s;

/* Set up the address struct */
if (*s) {
	/* Got name address */
	if (!(hp=gethostbyname(addr))) return ERR_UNKNOWN_HOST;
	memcpy((char *)&con_addr.sin_addr,hp->h_addr,hp->h_length);
	}
else {
	/* Number address */
	if ((con_addr.sin_addr.s_addr=inet_addr(addr))==-1) 
		return ERR_UNKNOWN_HOST;
	}
con_addr.sin_family=AF_INET;
con_addr.sin_port=htons(port);
if ((consock=socket(AF_INET,SOCK_STREAM,0))==-1) {
	sprintf(text,"Process %d (%s) got socket() error: %s",current_pcs->pid,current_pcs->name,sys_errlist[errno]);
	write_syslog(text);
	return ERR_SOCKET;
	}

/* Attempt to connect. Doing this will hang the system until the Avios or
   TCP timeout limit is reached, hence the console info messages. */
sprintf(text,"Process %d (%s) attempting connect to \"%s %d\"...",current_pcs->pid,current_pcs->name,addr,port);
write_syslog(text);

/* Set up alarm. If conect_timeout is 0 then we wait for TCP timeout so alarm
   not set */
conalarm_called=0;
if (connect_timeout) {
	signal(SIGALRM,conalarm);  alarm(connect_timeout);
	}

/* Attempt to connect to remote host until we timeout either via the
   alarm signal or via a TCP timeout (or via a SIGTERM but sod it.. :) */
if (connect(consock,&con_addr,sizeof(con_addr))==-1) {
	if (conalarm_called) sprintf(text,"Connect timed out.");
	else {
		sprintf(text,"Got connect() error: %s",sys_errlist[errno]);
		close(consock);
		}
	write_syslog(text);
	return ERR_CONNECT;
	}
signal(SIGALRM,SIG_IGN);
write_syslog("Connect succeeded.");

/* Set socket to non-blocking */
if ((fcntl(consock,F_SETFL,O_NONBLOCK))==-1) {
	sprintf(text,"Process %d (%s) got fcntl() error on socket: %s",current_pcs->pid,current_pcs->name,sys_errlist[errno]);
	write_syslog(text);
	close(consock);
	return ERR_SOCKET;
	}

/* Create stream */
sprintf(text,"SOCK_%d",consock);
if ((ret=create_stream(text,NULL,consock,-1,READWRITE,current_pcs))!=OK) {
	close(consock);  return ret;
	}
last_stream->con_sock=1;
 
/* Set eof var */
if ((ret=set_variable("$eof",NULL,"0",1))!=OK) {
	close(consock);  return ret;
	}
return push_rstack(text);
}



/*** Echo command which sends echo / noecho telopt codes to user
     client down a socket ***/
com_echo(pc)
int pc;
{
char *w,seq[4],val[2];
int com;

if (prog_word[pc].end_pos - pc!=1) return ERR_SYNTAX;
if (!is_sock_stream(current_instream)) goto SKIP; /* Can't set if not socket */

w=prog_word[pc+1].word;
if (!strcmp(w,"off")) com=0;
else if (!strcmp(w,"on")) com=1;
	else if (!strcmp(w,"val")) com=2;
		else return ERR_SYNTAX;

switch(com) {
	case 0:
	current_instream->echo=0;
	sprintf(seq,"%c%c%c",255,251,1);
	write_stream(seq);
	break;

	case 1:
	current_instream->echo=1;
	sprintf(seq,"%c%c%c",255,252,1);
	write_stream(seq);
	break;
	}
SKIP:
sprintf(val,"%d",current_instream->echo);
return push_rstack(val);
}

/********************* All things must come to THE END **********************/
