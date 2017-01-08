/******************************************************************************
                           AVIOS 1.5.1 header file
                   Copyright (C) Neil Robertson 1997-1998
 ******************************************************************************/

#define INIT_FILE "init"

#define NO 0
#define YES 1
#define OFF 0
#define ON 1

#define MAX_INCLUDE_LEVELS 10
#define MEMORY_RESERVE 500
#define NUM_COMS 128
#define NUM_ERRS 63
#define NUM_COLS 22
#define ARR_SIZE 5000
#define MAX_ARGC 100

#define MAX_PROCESSES 100
#define MAX_MESGS 20
#define MAX_ERRORS 1
#define MAX_EQUA_ELEMENTS 100
#define SWAPOUT_AFTER 10  /* Max no. instructions run before process swapout */
#define EXIT_REMAIN 5

/* Default permissions for mkdir command if no permission option given */
#define MKDIR_PERM_DEF 0755 

/* Internal stream types. There is only 1 at the moment. */
#define MESG_Q 0

/* File stuff */
#define INPT 0
#define OUTPT 1
#define READ 0
#define WRITE 1
#define READWRITE 2
#define APPEND 3

#define ALL_LEGAL 0
#define STRING_ILLEGAL 1
#define INT_ILLEGAL 2

/* Variable flags */
#define READONLY 1
#define ARRAY 2
#define STATIC 4
#define SHARED 8

/* Arg flags */
#define PCA 0  /* Program counter address */
#define PCV 1  /* value */

#define TAB 9

#define FREE(ptr)  if (ptr!=NULL) free(ptr)

#ifdef FREEBSD
extern const char *sys_errlist[];
#else
extern char *sys_errlist[];
#endif

/* Main system process structure. This stores all the structure pointers for
   a process and other bits. */
struct process {
	char *name,*filename,*site;
	int pid,pc,status,old_status;
	int swapout_after,over,num_words,local_port,remote_port;
	struct process *parent;

	struct program *prog_word;
	struct procedures *first_proc,*last_proc,*current_proc;
	struct loops_choose *first_loop,*last_loop,*current_loop;
	struct labels *first_label,*last_label;
	struct vars *first_var,*last_var,*current_var;
	struct proc_stack *pstack_start,*pstack_ptr;
	struct loop_stack *lstack_start,*lstack_ptr;
	struct result_stack *rstack_start,*rstack_ptr;
	struct streams *current_instream,*current_outstream;

	struct procedures *wait_var_proc,*c_int_proc,*n_int_proc,*t_int_proc;
	char *wait_var,*input_buff,*mesg_q,*death_mesg;
	int mesg_cnt,sleep_sec,sleep_usec,wait_pid,death_pid;
	int timer_sec,timer_usec,err_cnt,exit_cnt,input_vnum;
	int eof,exit_code,int_enabled,interrupted,colour;
	long unsigned int run_cnt[4];
	time_t created;

	struct process *prev,*next;
	} *first_pcs,*last_pcs,*current_pcs,*real_current_pcs;


/* Process states */
enum process_states {
	IMAGE,
	RUNNING, 
	HALTED,
	EXITING,
	OUTPUT_WAIT,
	INPUT_WAIT, 
	CHILD_DWAIT, 
	SPEC_DWAIT, 
	SLEEPING, 
	CHILD_INT,
	NONCHILD_INT,
	TIMER_INT
	};

char *status_name[]={
	"IMAGE",
	"RUNNING",
	"HALTED",
	"EXITING",
	"OUTPUT_WAIT",
	"INPUT_WAIT",
	"CHILD_DWAIT",
	"SPEC_DWAIT",
	"SLEEPING",
	"CHILD_INT",
	"NONCHILD_INT",
	"TIMER_INT"
	};


/*** This stores all the current listening ports and the processes they're
     linked to ***/
struct ports {
	short port;
	int listen_sock;
	struct process *process;
	struct ports *next;
	} *first_port,*last_port;
	

/* The system streams structure. Streams are all in one list (not one list
   per process like the others) to make searching for internal streams
   faster (else we'd have to load each process state in turn and search
   through each individual list) */
struct streams {
	char *name,*filename;
	int internal,external;
	int device,rw,block,con_sock,echo,locked;
	struct process *owner;
	struct streams *prev,*next;
	} *first_stream,*last_stream,*current_instream,*current_outstream;



/*** Interpreter specific structures. Each process has its own linked list
     of each of these structures so that we don't have to search one huge
     all encompasing list each time we want to find something in one of
     them. ***/

/* Each of these stores the actual program text of the process */
struct program {
	char *word,*filename;
	int real_line,prog_line;
	int op_num,com_num,end_pos;
	struct equa_struct *equa;
	int equacnt;
	} *prog_word; 

/* Used by the math command */
struct equa_struct {
        char *word;
        int value;
        char op;
        int pc,used;
        };

/* Procedure data */
struct procedures {
	char *name;
	int start_pos,ei_di_int,old_int;
	struct procedures *copy,*next;
	} *first_proc,*last_proc,*current_proc;

/* Loops (including choose command) data */
struct loops_choose {
	int type,for_to,foreach_pos;
	int start_pos,end_pos;
	struct vars *foreach_var;
	struct procedures *proc;
	struct loops_choose *copy,*next;
	} *first_loop,*last_loop,*current_loop;

/* Labels data */
struct labels {
	char *name;
	int pos;
	struct procedures *proc;
	struct labels *next;
	} *first_label,*last_label;

/* Variables data */
struct vars {
	char *name;
	char *value;
	char flags;
	struct procedures *proc; /* proc var is in if local */
	struct array *arr;
	struct vars *copy,*refvar,*prev,*next;
	} *first_var,*last_var,*current_var;

/* This is subsidiary to the struct above */
struct array {
	char *subscript,*value;
	struct array *next;
	};

/* STACKS */
struct proc_stack {
	struct procedures *proc;
	int ret_pc; /* place to jump back to in this proc after a call */ 
	struct proc_stack *prev,*next;
	} *pstack_start,*pstack_ptr;

struct loop_stack {
	struct loops_choose *loop;
	struct loop_stack *prev,*next;
	} *lstack_start,*lstack_ptr;

struct result_stack {
	char *value;
	struct result_stack *prev,*next;
	} *rstack_start,*rstack_ptr;

/* Forward declaration of command functions */
int com_alias(),com_var(),com_sharing(),com_stid(),com_maths1(),com_replace();
int com_strings2(),com_strings3(),com_vararg();
int com_locate(),com_cls(),com_goto(),com_label();
int com_if(),com_else(),com_endif(),com_while(),com_wend_next();
int com_do(),com_until(),com_for(),com_foreach();
int com_choose(),com_val_def(),com_chosen(),com_break(),com_continue();
int com_call(),com_proc(),com_return_endproc(),com_exit_sleep();
int com_input(),com_strings1(),com_count(),com_arrsize(),com_trap();
int com_streams(),com_open(),com_cdr(),com_seek(),com_dir();
int com_format(),com_spawn(),com_wait(),com_waitpid(),com_procs(),com_exec();
int com_onint(),com_interrupt(),com_timer(),com_ei_di(),com_gettime();
int com_colour(),com_connect(),com_echo(),com_math();

/* Commands structure. Arg_pass is used to see which args to pass when
   calling function pointer */
struct {
	char *command;
	int arg_pass;
	int (*func)();
	} commands[NUM_COMS]={
		"alias",PCA,com_alias,

		"var", PCV,com_var,
		"svar",PCV,com_var,

		"share",  PCV,com_sharing,
		"unshare",PCV,com_sharing,

		"set", PCV,com_stid,
		"tset",PCV,com_stid,
		"inc", PCV,com_stid,
		"dec", PCV,com_stid,

		"math",PCV,com_math,

		"not", PCV,com_maths1,
		"abs", PCV,com_maths1,
		"sgn", PCV,com_maths1,
		"rand",PCV,com_maths1,
		"cpl", PCV,com_maths1,

		"mulstr",  PCV,com_strings2,
		"substr",  PCV,com_strings3,
		"addstr",  PCV,com_vararg,
		"atoc",    PCV,com_vararg,
		"ctoa",    PCV,com_vararg,
		"max",     PCV,com_vararg,
		"min",     PCV,com_vararg,
		"maxstr",  PCV,com_vararg,
		"minstr",  PCV,com_vararg,
		"print",   PCV,com_vararg,
		"printnl", PCV,com_vararg,
		"printlog",PCV,com_vararg,

		"locate",PCV,com_locate,
		"cls"   ,PCV,com_cls,

		"goto", PCA,com_goto,
		"label",PCA,com_label,

		"if",   PCA,com_if,
		"else", PCA,com_else,
		"endif",PCA,com_endif,
		"fi",   PCA,com_endif,

		"while",   PCA,com_while,
		"wend",    PCA,com_wend_next,
		"do",      PCV,com_do,
		"until",   PCA,com_until,
		"for",     PCA,com_for,
		"next",    PCA,com_wend_next,
		"foreach", PCA,com_foreach,
		"nexteach",PCA,com_wend_next,

		"choose", PCA,com_choose,
		"value",  PCA,com_val_def,
		"default",PCA,com_val_def,
		"chosen", PCA,com_chosen,

		"break",    PCA,com_break,
		"continue", PCA,com_continue,
		"acontinue",PCA,com_continue,

		"call",   PCA,com_call,
		"vcall",  PCA,com_call,
		"proc",   PCA,com_proc,
		"endproc",PCA,com_return_endproc,
		"return", PCA,com_return_endproc,
		"exit",   PCV,com_exit_sleep,

		"sleep", PCV,com_exit_sleep,
		"usleep",PCV,com_exit_sleep,

		"input",PCV,com_input,

		"strlen",  PCV,com_strings1,
		"isnum",   PCV,com_strings1,
		"upperstr",PCV,com_strings1,
		"lowerstr",PCV,com_strings1,

		"replstr",   PCV,com_replace,
		"replelem",  PCV,com_replace,

		"instr",     PCV,com_strings3,
		"midstr",    PCV,com_strings3,
		"insertstr", PCV,com_strings3,
		"overstr",   PCV,com_strings3,
		"lpadstr",   PCV,com_strings3,
		"rpadstr",   PCV,com_strings3,
		"insertelem",PCV,com_strings3,
		"overelem",  PCV,com_strings3,
		"member",    PCV,com_strings3,
		"subelem",   PCV,com_strings3,
		"elements",  PCV,com_strings3,

		"count",PCV,com_count,

		"match"  , PCV,com_strings2,
		"unmatch", PCV,com_strings2,
		"matchstr",PCV,com_strings2,

		"unique",PCV,com_strings1,
		"head",  PCV,com_strings1,
		"rhead", PCV,com_strings1,
		"tail",  PCV,com_strings1,
		"rtail", PCV,com_strings1,

		"arrsize",PCV,com_arrsize,

		"trap",PCV,com_trap,

		"in",      PCV,com_streams,
		"out",     PCV,com_streams,
		"block",   PCV,com_streams,
		"nonblock",PCV,com_streams,
		"lock",    PCV,com_streams,
		"unlock",  PCV,com_streams,

		"open",  PCV,com_open,
		"close", PCV,com_cdr,
		"delete",PCV,com_cdr,
		"rmdir", PCV,com_cdr,
		"stat",  PCV,com_strings1,
		"rename",PCV,com_strings2,
		"copy",  PCV,com_strings2,
		"mkdir", PCV,com_strings2,
		"chmod", PCV,com_strings2,
		"cseek", PCV,com_seek,
		"lseek", PCV,com_seek,
		"dir",   PCV,com_dir,

		"format",PCV,com_format,

		"crypt",PCV,com_strings2,

		"spawn",   PCV,com_spawn,
		"wait",    PCV,com_wait,
		"waitpid", PCV,com_waitpid,
		"exists",  PCV,com_procs,
		"pcsinfo", PCV,com_procs,
		"relation",PCV,com_procs,
		"kill",    PCV,com_procs,
		"halt",    PCV,com_procs,
		"restart", PCV,com_procs,
		"exec",    PCV,com_exec,
		"iexec",   PCV,com_exec,

		"onint",    PCV,com_onint,
		"interrupt",PCV,com_interrupt,
		"timer",    PCV,com_timer,
		"utimer",   PCV,com_timer,
		"ei",       PCV,com_ei_di,
		"di",       PCV,com_ei_di,

		"gettime",PCV,com_gettime,

		"colour",PCV,com_colour,

		"connect",PCV,com_connect,

		"echo",PCV,com_echo
		};

enum command_vals {
	ALIAS,
	VAR,
	SVAR,
	SHARE,
	UNSHARE,
	SET,
	TSET,
	INC,
	DEC,
	MATH,
	NOTCOM,
	ABS,
	SGN,
	RAND,
	CPL,
	MULSTR,
	SUBSTR,
	ADDSTR,
	ATOC,
	CTOA,
	MAXCOM,
	MINCOM,
	MAXSTR,
	MINSTR,
	PRINT,
	PRINTNL,
	PRINTLOG,
	LOCATE,
	CLS,
	GOTO,
	LABEL,
	IF,
	ELSE,
	ENDIF,
	FI,
	WHILE,
	WEND,
	DO,
	UNTIL,
	FOR,
	NEXT,
	FOREACH,
	NEXTEACH,
	CHOOSE,
	VALUE,
	DEFAULT,
	CHOSEN,
	BREAK,
	CONTINUE,
	ACONTINUE,
	CALL,
	VCALL,
	PROC,
	ENDPROC,
	RETURN,
	EXIT,
	SLEEP,
	USLEEP,
	INPUT,
	STRLEN,
	ISNUM,
	UPPERSTR,
	LOWERSTR,
	REPLSTR,
	REPLELEM,
	INSTR,
	MIDSTR,
	INSERTSTR,
	OVERSTR,
	LPADSTR,
	RPADSTR,
	INSERTELEM,
	OVERELEM,
	MEMBER,
	SUBELEM,
	ELEMENTS,
	COUNT,
	MATCH,
	UNMATCH,
	MATCHSTR,
	UNIQUE,
	HEAD,
	RHEAD,
	TAIL,
	RTAIL,
	ARRSIZE,	
	TRAP,
	IN,
	OUT,
	BLOCK,
	NONBLOCK,
	LOCK,
	UNLOCK,
	OPEN,
	CLOSE,
	DELETE,
	RMDIR,
	STAT,
	RENAME,
	COPY,
	MKDIR,
	CHMOD,
	CSEEK,
	LSEEK,
	DIRCOM,
	FORMAT,
	CRYPT,
	SPAWN,
	WAIT,
	WAITPID,
	EXISTS,
	PCSINFO,
	RELATION,
	KILL,
	HALT,
	RESTART,
	EXEC,
	IEXEC,
	ONINT,
	INTERRUPT,
	TIMER,
	UTIMER,
	EI,
	DI,
	GETTIME,
	COLOUR,
	CONNECT,
	ECHOCOM
	};


enum ops {
	/* Single char ops */
	LEFT_BRACKET,
	RIGHT_BRACKET,
	LESS_THAN,
	GREATER_THAN,
	NOT,
	EQUALS,

	/* Multi char ops */
	LESS_THAN_EQUALS,
	GREATER_THAN_EQUALS,
	NOT_EQUAL
	};


char *error_mesg[NUM_ERRS]={
	"Ok",
	"Internal error",
	"Memory allocation error",
	"Missing bracket",
	"Line too long",
	"Unterminated string",
	"Unexpected EOF",
	"Max include file nesting level exceeded",
	"Main procedure missing",
	"Syntax error",
	"Invalid argument",
	"Too many arguments",
	"Illegal character in string",
	"Undefined label",
	"Undefined variable",
	"Undefined subscript",
	"Undefined procedure called",
	"Recursion not supported",
	"Array element not found",
	"Invalid variable name",
	"Invalid subscript name",
	"Variable already exists",
	"Variable is read only",
	"Variable is not an array",
	"Variable is an array",
	"Variable is not shared",
	"Variable cannot be shared",
	"Local variables cannot be shared",
	"Cannot negate a non numeric value",
	"Unexpected command",
	"Unterminated loop",
	"Duplicate label",
	"Duplicate procedure name",
	"Invalid procedure name",
	"Missing ENDIF or FI",
	"Parameter count mismatch",
	"Parameter is an array",
	"Command cannot be nested",
	"Invalid pass by reference",
	"Invalid stream",
	"Invalid stream direction",
	"Unset stream",
	"Write failed on stream",
	"Cannot open file or directory",
	"Cannot delete file or directory",
	"Cannot rename file",
	"Cannot stat filesystem entry",
	"Cannot change filesystem entry permissions",
	"File is in use",
	"Cannot create directory",
	"Maximum process count reached",
	"No such process id",
	"Interrupt procedure cannot have arguments",
	"Unknown host",
	"Error while creating or setting socket",
	"Connect failure",
	"Cannot create terminal process if system is running as a daemon",
	"Cannot stat device",
	"Device is not a character device",
	"Cannot open device",
	"String too long",
	"Equation too complex",
	"Division by zero"
	};


enum error_codes {
	OK,
	ERR_INTERNAL,
	ERR_MALLOC,
	ERR_MISSING_BRACKET,
	ERR_LINE_TOO_LONG,
	ERR_UNTERMINATED_STRING,
	ERR_UNEXPECTED_EOF,
	ERR_MAX_INCLUDE,
	ERR_MAIN_PROC_MISSING,
	ERR_SYNTAX,
	ERR_INVALID_ARGUMENT,
	ERR_TOO_MANY_ARGS,
	ERR_ILLEGAL_CHAR,
	ERR_UNDEF_LABELNAME,
	ERR_UNDEF_VAR,
	ERR_UNDEF_SUBSCRIPT,
	ERR_UNDEF_PROC,
	ERR_RECURSION,
	ERR_ELEMENT_NOT_FOUND,
	ERR_INVALID_VAR,
	ERR_INVALID_SUBSCRIPT,
	ERR_VAR_ALREADY_EXISTS,
	ERR_VAR_IS_READONLY,
	ERR_VAR_IS_NOT_ARRAY,
	ERR_VAR_IS_ARRAY,
	ERR_VAR_NOT_SHARED,
	ERR_VAR_SHARE1,
	ERR_VAR_SHARE2,
	ERR_CANNOT_NEGATE,
	ERR_UNEXPECTED_COMMAND,
	ERR_UNTERMINATED_LOOP,
	ERR_DUPLICATE_LABEL,
	ERR_DUPLICATE_PROCNAME,
	ERR_INVALID_PROCNAME,
	ERR_MISSING_ENDIF,
	ERR_PARAM_COUNT,
	ERR_PARAM_IS_ARRAY,
	ERR_NESTED_COMMAND,
	ERR_INVALID_REF_PASS,
	ERR_INVALID_STREAM,
	ERR_INVALID_STREAM_DIR,
	ERR_UNSET_STREAM,
	ERR_WRITE_FAILED,
	ERR_CANT_OPEN_FILE_OR_DIR,
	ERR_CANT_DELETE_FILE_OR_DIR,
	ERR_CANT_RENAME_FILE,
	ERR_CANT_STAT_FS_ENTRY,
	ERR_CANT_CHANGE_FS_ENTRY_PERM,
	ERR_FILE_IN_USE,
	ERR_CANT_CREATE_DIR,
	ERR_MAX_PROCESSES,
	ERR_NO_SUCH_PROCESS,
	ERR_INT_PROC_ARGS,
	ERR_UNKNOWN_HOST,
	ERR_SOCKET,
	ERR_CONNECT,
	ERR_SYS_IS_DAEMON,
	ERR_CANT_STAT_DEVICE,
	ERR_DEVICE_NOT_CHAR,
	ERR_CANT_OPEN_DEVICE,
	ERR_STRING_TOO_LONG,
	ERR_EQUATION_TOO_COMPLEX,
	ERR_DIVISION_BY_ZERO
	};

char *colcode[NUM_COLS]={
/* Non-colour codes: reset, bold, underline, blink, reverse, cls */
"\033[0m", "\033[1m", "\033[4m", "\033[5m", "\033[7m","\033[H\033[J",

/* Foreground colours: black, red, green, yellow */
"\033[30m","\033[31m","\033[32m","\033[33m",
"\033[34m","\033[35m","\033[36m","\033[37m",

/* Background colours: blue, magenta, turquoise, white */
"\033[40m","\033[41m","\033[42m","\033[43m",
"\033[44m","\033[45m","\033[46m","\033[47m"
};

/* Codes used in a string to produce the colours when prepended with a '~' */
char *colcom[NUM_COLS]={
"RS","OL","UL","LI","RV","CL",
"FK","FR","FG","FY",
"FB","FM","FT","FW",
"BK","BR","BG","BY",
"BB","BM","BT","BW"
};

#ifdef SUN_BSD_BUG
/* The structure the bsd libs require. This is set up incorrectly in
   the Solaris header files for the BSD readdir func as it contains an extra
   field which it doesn't want which in turn causes incorrect data. */
typedef struct {
	ino_t d_ino;      /* "inode number" of entry */
	off_t d_off;      /* offset of disk directory entry */
	char  d_name[1];  /* A pointer won't work here for some reason */
	}* avios_dirent;
#else
typedef struct dirent* avios_dirent;
#endif

/* Other globals */
int num_words,be_daemon,max_processes,max_mesgs,max_errors,exit_remain;
int swapout_after,process_count,memory_reserve,eval_result,real_line;
int inst_cnt,colour_def,kill_any,child_die,ignore_sigterm,wait_on_dint;
int pause_on_sigtstp,consock,conalarm_called,connect_timeout;
int allow_ur_path,tuning_delay,qbm,enhanced_dump,avios_cont;

time_t boottime;
struct timeval avtime;

char build[50],*syslog_file,*init_file;
char text[ARR_SIZE+1],text2[ARR_SIZE+1];
char code_path[200],root_path[200],*incf;

