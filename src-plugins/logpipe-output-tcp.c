#include "logpipe_api.h"

struct LogpipeOutputPlugin_tcp
{
	struct LogpipeEnv		*p_env ;
	struct LogpipeInputPlugin	*p_logpipe_input_plugin ;
	
	char				*ip = NULL ;
	int				port ;
	
	struct sockaddr_in   	 	forward_addr ;
	int				forward_sock ;
	
} ;

static int ConnectForwardSocket( struct LogpipeOutputPlugin_tcp *p_plugin_env )
{
	int		nret = 0 ;
	
	/* �����׽��� */
	p_plugin_env->forward_sock = socket( AF_INET , SOCK_STREAM , IPPROTO_TCP ) ;
	if( p_plugin_env->forward_sock == -1 )
	{
		ERRORLOG( "socket failed , errno[%d]" , errno );
		sleep(1);
		return -1;
	}
	
	/* �����׽���ѡ�� */
	{
		int	onoff = 1 ;
		setsockopt( p_plugin_env->forward_sock , SOL_SOCKET , SO_REUSEADDR , (void *) & onoff , sizeof(int) );
	}
	
	{
		int	onoff = 1 ;
		setsockopt( p_plugin_env->forward_sock , IPPROTO_TCP , TCP_NODELAY , (void*) & onoff , sizeof(int) );
	}
	
	/* ���ӵ�����������˿� */
	nret = connect( p_plugin_env->forward_sock , (struct sockaddr *) & (p_plugin_env->forward_addr) , sizeof(struct sockaddr) ) ;
	if( nret == -1 )
	{
		ERRORLOG( "connect[%s:%d] failed , errno[%d]" , p_plugin_env->forward_ip , p_plugin_env->forward_port , errno );
		close( p_plugin_env->forward_sock ); p_plugin_env->forward_sock = -1 ;
		sleep(1);
		return 1;
	}
	else
	{
		INFOLOG( "connect[%s:%d] ok , sock[%d]" , p_plugin_env->forward_ip , p_plugin_env->forward_port , p_plugin_env->forward_sock );
		return 0;
	}
}

funcInitLogpipeOutputPlugin InitLogpipeOutputPlugin ;
int InitLogpipeOutputPlugin( struct LogpipeEnv *p_env , struct LogpipePluginConfigItem *p_plugin_config_items , void **pp_context )
{
	struct LogpipeOutputPlugin_tcp	*p_plugin_env = NULL ;
	char				*p = NULL ;
	
	int				nret = 0 ;
	
	p_plugin_env = (struct LogpipeOutputPlugin_tcp *)malloc( sizeof(struct LogpipeOutputPlugin_tcp) ) ;
	if( p_plugin_env == NULL )
	{
		ERRORLOG( "malloc failed , errno[%d]" , errno );
		return -1;
	}
	memset( p_plugin_env , 0x00 , sizeof(struct LogpipeOutputPlugin_tcp) );
	
	/* ����������� */
	p_plugin_env->ip = QueryPluginConfigItem( p_plugin_config_items , "ip" ) ;
	
	p = QueryPluginConfigItem( p_plugin_config_items , "port" ) ;
	if( p )
		p_plugin_env->port = atoi(p) ;
	else
		p_plugin_env->port = 0 ;
	
	/* ��ʼ����������ڲ����� */
	memset( & (p_plugin_env->forward_addr) , 0x00 , sizeof(struct sockaddr_in) );
	p_plugin_env->forward_addr.sin_family = AF_INET ;
	if( p_plugin_env->ip[0] == '\0' )
		p_plugin_env->forward_addr.sin_addr.s_addr = INADDR_ANY ;
	else
		p_plugin_env->forward_addr.sin_addr.s_addr = inet_addr(p_plugin_env->ip) ;
	p_plugin_env->forward_addr.sin_port = htons( (unsigned short)(p_plugin_env->port) );
	
	/* ���ò������������ */
	(*pp_context) = p_plugin_env ;
	
	/* ���ӷ���� */
	p_plugin_env->forward_sock = -1 ;
	nret = ConnectForwardSocket( p_plugin_env ) ;
	if( nret )
		return nret;
	
	return 0;
}

funcBeforeWriteLogpipeOutput BeforeWriteLogpipeOutput ;
int BeforeWriteLogpipeOutput( struct LogpipeEnv *p_env , void *p_context , uint16_t filename_len , char *filename )
{
	struct LogpipeOutputPlugin_tcp	*p_plugin_env = (struct LogpipeOutputPlugin_tcp *)p_context ;
	
	uint16_t			*filename_len_htons = NULL ;
	char				comm_buf[ 1 + sizeof(uint16_t) + PATH_MAX ] ;
	
_GOTO_RETRY_SEND :
	
	while( p_plugin_env->forward_sock == -1 )
	{
		nret = ConnectForwardSocket( p_plugin_env ) ;
		if( nret < 0 )
			return nret;
	}
	
	memset( comm_buf , 0x00 , sizeof(comm_buf) );
	comm_buf[0] = LOGPIPE_COMM_HEAD_MAGIC ;
	
	if( filename_len > PATH_MAX )
	{
		ERRORLOG( "filename length[%d] too long" , filename_len )
		return 0;
	}
	
	filename_len_htons = (uint16_t*)(comm_buf+1) ;
	(*filename_len_htons) = htons(filename_len) ;
	
	strncpy( comm_buf+1+sizeof(uint16_t) , filename , filename_len );
	
	/* ����ͨѶͷ���ļ��� */
	len = writen( p_plugin_env->forward_sock , comm_buf , 1+sizeof(uint16_t)+filename_len ) ;
	if( len == -1 )
	{
		ERRORLOG( "send comm magic and filename failed , errno[%d]" , errno )
		close( p_plugin_env->forward_sock ); p_plugin_env->forward_sock = -1 ;
		sleep(1);
		goto _GOTO_RETRY_SEND;
	}
	else
	{
		INFOLOG( "send comm magic and filename ok , [%d]bytes" , 1+sizeof(uint16_t)+filename_len )
		DEBUGHEXLOG( comm_buf , len , NULL )
	}
	
	return 0;
}

funcWriteLogpipeOutput WriteLogpipeOutput ;
int WriteLogpipeOutput( struct LogpipeEnv *p_env , void *p_context , uint32_t block_len , char *block_buf )
{
	struct LogpipeOutputPlugin_tcp	*p_plugin_env = (struct LogpipeOutputPlugin_tcp *)p_context ;
	
	uint32_t			block_len_htonl ;
	int				len ;
	
	block_len_htonl = htonl(block_len) ;
	len = writen( p_plugin_env->forward_sock , & block_len_htonl , sizeof(block_len_htonl) ) ;
	if( len == -1 )
	{
		ERRORLOG( "send block len to socket failed , errno[%d]" , errno )
		return -1;
	}
	else
	{
		INFOLOG( "send block len to socket ok , [%d]bytes" , sizeof(block_len_htonl) )
		DEBUGHEXLOG( (char*) & block_len_htonl , len , NULL )
	}
	
	len = writen( p_plugin_env->forward_sock , block_buf , block_len ) ;
	if( len == -1 )
	{
		ERRORLOG( "send block data to socket failed , errno[%d]" , errno )
		return -1;
	}
	else
	{
		INFOLOG( "send block data to socket ok , [%d]bytes" , block_len )
		DEBUGHEXLOG( block_buf , len , NULL )
	}
	
	return 0;
}

func AfterWriteLogpipeOutput AfterWriteLogpipeOutput ;
int AfterWriteLogpipeOutput( struct LogpipeEnv *p_env , void *p_context )
{
	return 0;
}

funcCleanLogpipeOutputPlugin CleanLogpipeOutputPlugin ;
int CleanLogpipeOutputPlugin( struct LogpipeEnv *p_env , void *p_context )
{
	struct LogpipeOutputPlugin_tcp	*p_plugin_env = (struct LogpipeOutputPlugin_tcp *)p_context ;
	
	INFOLOG( "free p_plugin_env" )
	free( p_plugin_env );
	
	return 0;
}
