/******************************************************************************
                           AVIOS 1.0.0 header file
                      Copyright (C) Neil Robertson 1997
 ******************************************************************************/

#define INIT_FILE "init"

#define MEMORY_RESERVE 500
#define NUM_COMS 111
#define NUM_ERRS 54
#define NUM_COLS 21
#define ARR_SIZE 5000
#define MAX_ARGC 100

#define MAX_PROCESSES 100
#define MAX_MESGS 20
#define MAX_ERRORS 1
#define SWAPOUT_AFTER 10  /* Max no. instructions run before process swapout */
#define EXIT_REMAIN 5
#define COLOUR_DEF 1
#define IGNORE_SIGTERM 0
#define KILL_ANY 0
#define CHILD_DIE 0
#define WAIT_ON_DINT 1
#define ALLOW_UR_PATH 1

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

#define TAB 9

#define FREE(ptr)  if (ptr!=NULL) free(ptr)


/* Main system process structure. This stores all the structure pointers for
   a process and other bits. */
struct process {
	char *name,*filename,*site;
	int pid,pc,status,old_status;
	int over,num_words,local_port,remote_port;
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
	int mesg_cnt,sleep_to,wait_pid,death_pid;
	int timer_exp,err_cnt,exit_cnt,input_vnum;
	int eof,exit_code,int_enabled,interrupted,colour;
	time_t created;

	struct process *prev,*next;
	} *first_pcs,*last_pcs,*current_pcs,*real_current_pcs;


/* Process states */
enum process_states {
	IMAGE,
	RUNNING, 
	OUTPUT_WAIT,
	INPUT_WAIT, 
	CHILD_DWAIT, 
	SPEC_DWAIT, 
	SLEEPING, 
	CHILD_INT,
	NONCHILD_INT,
	TIMER_INT,
	EXITING
	};

char *status_name[]={
	"IMAGE",
	"RUNNING",
	"OUTPUT_WAIT",
	"INPUT_WAIT",
	"CHILD_DWAIT",
	"SPEC_DWAIT",
	"SLEEPING",
	"CHILD_INT",
	"NONCHILD_INT",
	"TIMER_INT",
	"EXITING"
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
	int rw,block,con_sock,echo,locked;
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
	} *prog_word; 

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
	int ret_pc; /* place to jump back to in this proc after calling another */ 
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


/* This order here is important as its used by exec_command() to see if a
   command can be nested */
char *commands[NUM_COMS]={
	"alias",
	"var",
	"svar",
	"share",
	"unshare",
	"set",
	"inc",
	"dec",
	"not",
	"abs",
	"sgn",
	"rand",
	"mulstr",
	"substr",
	"addstr",
	"add",
	"sub",
	"mul",
	"div",
	"mod",
	"atoc",
	"ctoa",
	"max",
	"min",
	"maxstr",
	"minstr",
	"print",
	"printnl",
	"goto",
	"label",
	"if",
	"else",
	"endif",
	"while",
	"wend",
	"do",
	"until",
	"for",
	"next",
	"foreach",
	"nexteach",
	"choose",
	"value",
	"default",
	"chosen",
	"break",
	"continue",
	"call",
	"proc",
	"endproc",
	"return",
	"exit",
	"sleep",
	"input",
	"strlen",
	"isnum",
	"upperstr",
	"lowerstr",
	"instr",
	"midstr",
	"insertstr",
	"overstr",
	"lpadstr",
	"rpadstr",
	"insertelem",
	"overelem",
	"member",
	"subelem",
	"elements",
	"count",
	"match",
	"unique",
	"head",
	"rhead",
	"tail",
	"rtail",
	"arrsize",
	"trap",
	"in",
	"out",
	"block",
	"nonblock",
	"lock",
	"unlock",
	"open",
	"close",
	"delete",
	"rename",
	"copy",
	"cseek",
	"lseek",
	"dir",
	"format",
	"crypt",
	"spawn",
	"wait",
	"waitpid",
	"exists",
	"pcsinfo",
	"relation",
	"kill",
	"exec",
	"onint",
	"interrupt",
	"timer",
	"ei",
	"di",
	"gettime",
	"colour",
	"connect",
	"echo"
	};

enum command_vals {
	ALIAS,
	VAR,
	SVAR,
	SHARE,
	UNSHARE,
	SET,
	INC,
	DEC,
	NOTCOM,
	ABS,
	SGN,
	RAND,
	MULSTR,
	SUBSTR,
	ADDSTR,
	ADD,
	SUB,
	MUL,
	DIV,
	MOD,
	ATOC,
	CTOA,
	MAXCOM,
	MINCOM,
	MAXSTR,
	MINSTR,
	PRINT,
	PRINTNL,
	GOTO,
	LABEL,
	IF,
	ELSE,
	ENDIF,
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
	CALL,
	PROC,
	ENDPROC,
	RETURN,
	EXIT,
	SLEEP,
	INPUT,
	STRLEN,
	ISNUM,
	UPPERSTR,
	LOWERSTR,
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
	RENAME,
	COPY,
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
	EXEC,
	ONINT,
	INTERRUPT,
	TIMER,
	EI,
	DI,
	GETTIME,
	COLOUR,
	CONNECT,
	ECHO
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
	"Program too big",
	"Missing bracket",
	"Line too long",
	"Unterminated string",
	"Unexpected EOF",
	"Only 1 level of file inclusion permitted",
	"Main procedure missing",
	"Syntax error",
	"Invalid argument",
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
	"Unexpected command",
	"Unterminated loop",
	"Duplicate label",
	"Duplicate procedure name",
	"Invalid procedure name",
	"Missing ENDIF",
	"Parameter count mismatch",
	"Parameter is an array",
	"Command cannot be nested",
	"Invalid pass by reference",
	"Invalid stream",
	"Invalid stream direction",
	"Unset stream",
	"Write failed on stream",
	"Cannot open file",
	"Cannot delete file",
	"Cannot rename file",
	"File is in use",
	"Cannot open directory",
	"Maximum process count reached",
	"No such process id",
	"Process cannot be killed",
	"Interrupt procedure cannot have arguments",
	"Unknown host",
	"Error while creating or setting socket",
	"Connect failure"
	};


enum error_codes {
	OK,
	ERR_INTERNAL,
	ERR_MALLOC,
	ERR_PROG_TOO_BIG,
	ERR_MISSING_BRACKET,
	ERR_LINE_TOO_LONG,
	ERR_UNTERMINATED_STRING,
	ERR_UNEXPECTED_EOF,
	ERR_INCLUDE_LIMIT,
	ERR_MAIN_PROC_MISSING,
	ERR_SYNTAX,
	ERR_INVALID_ARGUMENT,
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
	ERR_CANT_OPEN_FILE,
	ERR_CANT_DELETE_FILE,
	ERR_CANT_RENAME_FILE,
	ERR_FILE_IN_USE,
	ERR_CANT_OPEN_DIR,
	ERR_MAX_PROCESSES,
	ERR_NO_SUCH_PROCESS,
	ERR_CANT_KILL,
	ERR_INT_PROC_ARGS,
	ERR_UNKNOWN_HOST,
	ERR_SOCKET,
	ERR_CONNECT
	};

char *colcode[NUM_COLS]={
/* Non-colour codes: reset, bold, underline, blink, reverse */
"\033[0m", "\033[1m", "\033[4m", "\033[5m", "\033[7m",

/* Foreground colours: black, red, green, yellow */
"\033[30m","\033[31m","\033[32m","\033[33m",
"\033[34m","\033[35m","\033[36m","\033[37m",

/* Background colours: blue, magenta, turquoise, white */
"\033[40m","\033[41m","\033[42m","\033[43m",
"\033[44m","\033[45m","\033[46m","\033[47m"
};

/* Codes used in a string to produce the colours when prepended with a '~' */
char *colcom[NUM_COLS]={
"RS","OL","UL","LI","RV",
"FK","FR","FG","FY",
"FB","FM","FT","FW",
"BK","BR","BG","BY",
"BB","BM","BT","BW"
};


/* Globals that map onto process struct elements */
int num_words;

/* Other globals */
int daemon,max_processes,max_mesgs,max_errors,exit_remain,swapout_after;
int process_count,memory_reserve,eval_result,real_line,inst_cnt;
int colour_def,kill_any,child_die,ignore_sigterm,wait_on_dint;
int consock,conalarm_called,connect_timeout,allow_ur_path;

time_t sysboot;

char *syslog,*init_file;
char text[ARR_SIZE+1],text2[ARR_SIZE+1];
char code_path[200],root_path[200],*incf;
extern char *sys_errlist[];

