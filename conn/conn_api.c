
#include "wt_internal.h"

static int __conn_statistics_config(WT_SESSION_IMPL*, const char*[]);

/*外部调用比较器函数接口*/
static int ext_collate(WT_EXTENSION_API* wt_api, WT_SESSION* wt_session, WT_COLLATOR* collator, WT_ITEM* first, WT_ITEM* second, int* cmpp)
{
	WT_CONNECTION_IMPL *conn;
	WT_SESSION_IMPL *session;

	/*如果传入的session是NULL，直接用connection默认session*/
	conn = (WT_CONNECTION_IMPL* )wt_api->conn;
	if ((session = (WT_SESSION_IMPL*)wt_session) == NULL)
		session = conn->default_session;

	WT_RET(__wt_compare(session, collator, first, second, cmpp));

	return 0;
}

/*根据uri和cfg_arg对collator进行配置*/
static int ext_collator_config(WT_EXTENSION_API* wt_api, WT_SESSION* wt_session, const char* uri, WT_CONFIG_ARG* cfg_arg, WT_COLLATOR** collatorp, int* ownp)
{
	WT_CONFIG_ITEM cval, metadata;
	WT_CONNECTION_IMPL *conn;
	WT_SESSION_IMPL *session;
	const char **cfg;

	conn = (WT_CONNECTION_IMPL*)wt_api->conn;
	if ((session = (WT_SESSION_IMPL*)wt_session) == NULL)
		session = conn->default_session;

	if ((cfg = (const char**)cfg_arg) == NULL)
		return 0;

	WT_CLEAR(cval);

	WT_RET_NOTFOUND_OK(__wt_config_gets_none(session, cfg, "collator", &cval));
	if (cval.len == 0)
		return 0;

	WT_CLEAR(metadata);
	WT_RET_NOTFOUND_OK(__wt_config_gets(session, cfg, "app_metadata", &metadata));

	/*配置collator*/
	return __wt_collator_config(session, uri, &cval, &metadata, collatorp, ownp);
}

/*检查自定义的collator是否已经在connection中注册*/
static int __collator_confchk(WT_SESSION_IMPL* session, WT_CONFIG_ITEM* cname, WT_COLLATOR** collatorp)
{
	WT_CONNECTION_IMPL* conn;
	WT_NAMED_COLLATOR* ncoll;

	if (collatorp != NULL)
		*collatorp = NULL;

	if (cname->len == 0 || WT_STRING_MATCH("none", cname->str, cname->len))
		return 0;

	conn = S2C(session);
	TAILQ_FOREACH(ncoll, &conn->collqh, q){
		if (WT_STRING_MATCH(ncoll->name, cname->str, cname->len)) { /*根据比较器名字在connection的比较器队列中查找，如果有直接返回*/
			if (collatorp != NULL)
				*collatorp = ncoll->collator;
			return 0;
		}
	}

	WT_RET_MSG(session, EINVAL, "unknown collator '%.*s'", (int)cname->len, cname->str);
}

int __wt_collator_confchk(WT_SESSION_IMPL *session, WT_CONFIG_ITEM *cname)
{
	return __collator_confchk(session, cname, NULL);
}

int __wt_collator_config(WT_SESSION_IMPL* session, const char* uri, WT_CONFIG_ITEM* cname, WT_CONFIG_ITEM* metadata, WT_COLLATOR** collatorp, int* ownp)
{
	WT_COLLATOR* collator;

	*collatorp = NULL;
	*ownp = 0;

	/*检查cname命名的比较器是否已经存在注册*/
	WT_RET(__collator_confchk(session, cname, &collator));
	if (collator == NULL)
		return 0;

	/*进行配置*/
	if (collator->customize != NULL)
		WT_RET(collator->customize(collator, &session->iface, uri, metadata, collatorp));

	if (*collatorp == NULL)
		*collatorp = collator;
	else
		*ownp = 1;

	return 0;
}

/*外部调用获取内部api调用接口*/
static WT_EXTENSION_API * __conn_get_extension_api(WT_CONNECTION *wt_conn)
{
	WT_CONNECTION_IMPL *conn;

	conn = (WT_CONNECTION_IMPL *)wt_conn;

	conn->extension_api.conn = wt_conn;
	conn->extension_api.err_printf = __wt_ext_err_printf;
	conn->extension_api.msg_printf = __wt_ext_msg_printf;
	conn->extension_api.strerror = __wt_ext_strerror;
	conn->extension_api.scr_alloc = __wt_ext_scr_alloc;
	conn->extension_api.scr_free = __wt_ext_scr_free;
	conn->extension_api.collator_config = ext_collator_config;
	conn->extension_api.collate = ext_collate;
	conn->extension_api.config_parser_open = __wt_ext_config_parser_open;
	conn->extension_api.config_get = __wt_ext_config_get;
	conn->extension_api.metadata_insert = __wt_ext_metadata_insert;
	conn->extension_api.metadata_remove = __wt_ext_metadata_remove;
	conn->extension_api.metadata_search = __wt_ext_metadata_search;
	conn->extension_api.metadata_update = __wt_ext_metadata_update;
	conn->extension_api.struct_pack = __wt_ext_struct_pack;
	conn->extension_api.struct_size = __wt_ext_struct_size;
	conn->extension_api.struct_unpack = __wt_ext_struct_unpack;
	conn->extension_api.transaction_id = __wt_ext_transaction_id;
	conn->extension_api.transaction_isolation_level = __wt_ext_transaction_isolation_level;
	conn->extension_api.transaction_notify = __wt_ext_transaction_notify;
	conn->extension_api.transaction_oldest = __wt_ext_transaction_oldest;
	conn->extension_api.transaction_visible = __wt_ext_transaction_visible;
	conn->extension_api.version = wiredtiger_version;

	return (&conn->extension_api);
}

#ifdef HAVE_BUILTIN_EXTENSION_SNAPPY
extern int snappy_extension_init(WT_CONNECTION *, WT_CONFIG_ARG *);
#endif
#ifdef HAVE_BUILTIN_EXTENSION_ZLIB
extern int zlib_extension_init(WT_CONNECTION *, WT_CONFIG_ARG *);
#endif
#ifdef HAVE_BUILTIN_EXTENSION_LZ4
extern int lz4_extension_init(WT_CONNECTION *, WT_CONFIG_ARG *);
#endif

/*加载默认的扩展模块，主要是初始化压缩算法*/
static int __conn_load_default_extensions(WT_CONNECTION_IMPL* conn)
{
	WT_UNUSED(conn);
#ifdef HAVE_BUILTIN_EXTENSION_SNAPPY
	WT_RET(snappy_extension_init(&conn->iface, NULL));
#endif
#ifdef HAVE_BUILTIN_EXTENSION_ZLIB
	WT_RET(zlib_extension_init(&conn->iface, NULL));
#endif
#ifdef HAVE_BUILTIN_EXTENSION_LZ4
	WT_RET(lz4_extension_init(&conn->iface, NULL));
#endif
	return 0;
}

/*动态加载扩展模块动态库*/
static int __conn_load_extension(WT_CONNECTION* wt_conn, const char* path, const char* config)
{
	WT_CONFIG_ITEM cval;
	WT_CONNECTION_IMPL *conn;
	WT_DECL_RET;
	WT_DLH *dlh;
	WT_SESSION_IMPL *session;
	int(*load)(WT_CONNECTION *, WT_CONFIG_ARG *);
	int is_local;
	const char *init_name, *terminate_name;

	dlh = NULL;
	init_name = terminate_name = NULL;
	is_local = strcmp(path, "local") == 0;

	conn = (WT_CONNECTION_IMPL *)wt_conn;
	CONNECTION_API_CALL(conn, session, load_extension, config, cfg);

	WT_ERR(__wt_dlopen(session, is_local ? NULL : path, &dlh));

	/*
	* Find the load function, remember the unload function for when we close.
	*/
	WT_ERR(__wt_config_gets(session, cfg, "entry", &cval));
	WT_ERR(__wt_strndup(session, cval.str, cval.len, &init_name));
	WT_ERR(__wt_dlsym(session, dlh, init_name, 1, &load));

	WT_ERR(__wt_config_gets(session, cfg, "terminate", &cval));
	WT_ERR(__wt_strndup(session, cval.str, cval.len, &terminate_name));
	WT_ERR(__wt_dlsym(session, dlh, terminate_name, 0, &dlh->terminate));

	/* Call the load function last, it simplifies error handling. */
	WT_ERR(load(wt_conn, (WT_CONFIG_ARG *)cfg));

	/* Link onto the environment's list of open libraries. */
	__wt_spin_lock(session, &conn->api_lock);
	TAILQ_INSERT_TAIL(&conn->dlhqh, dlh, q);
	__wt_spin_unlock(session, &conn->api_lock);
	dlh = NULL;

err:	
	if (dlh != NULL)
		WT_TRET(__wt_dlclose(session, dlh));
	__wt_free(session, init_name);
	__wt_free(session, terminate_name);

	API_END_RET_NOTFOUND_MAP(session, ret);
}

/*加载所有的扩展模块*/
static int __conn_load_extensions(WT_SESSION_IMPL* session, const char* cfg[])
{
	WT_CONFIG subconfig;
	WT_CONFIG_ITEM cval, skey, sval;
	WT_CONNECTION_IMPL *conn;
	WT_DECL_ITEM(exconfig);
	WT_DECL_ITEM(expath);
	WT_DECL_RET;

	conn = S2C(session);
	/*加载默认扩展模块*/
	WT_ERR(__conn_load_default_extensions(conn));

	/*加载配置项中的扩展模块*/
	WT_ERR(__wt_config_gets(session, cfg, "extensions", &cval));
	WT_ERR(__wt_config_subinit(session, &subconfig, &cval));
	while ((ret = __wt_config_next(&subconfig, &skey, &sval)) == 0) {
		if (expath == NULL)
			WT_ERR(__wt_scr_alloc(session, 0, &expath));
		WT_ERR(__wt_buf_fmt(session, expath, "%.*s", (int)skey.len, skey.str));
		if (sval.len > 0) {
			if (exconfig == NULL)
				WT_ERR(__wt_scr_alloc(session, 0, &exconfig));
			WT_ERR(__wt_buf_fmt(session, exconfig, "%.*s", (int)sval.len, sval.str));
		}
		WT_ERR(conn->iface.load_extension(&conn->iface, expath->data, (sval.len > 0) ? exconfig->data : NULL));
	}
	WT_ERR_NOTFOUND_OK(ret);

err:
	__wt_scr_free(session, &expath);
	__wt_scr_free(session, &exconfig);

	return ret;
}

/*为connection增加一个比较器，外部调用接口*/
static int __conn_add_collator(WT_CONNECTION* wt_conn, const char* name, WT_COLLATOR* collator, const char* config)
{
	WT_CONNECTION_IMPL *conn;
	WT_DECL_RET;
	WT_NAMED_COLLATOR *ncoll;
	WT_SESSION_IMPL *session;

	ncoll = NULL;

	conn = (WT_CONNECTION_IMPL *)wt_conn;
	CONNECTION_API_CALL(conn, session, add_collator, config, cfg);
	WT_UNUSED(cfg);

	if (WT_STREQ(name, "none"))
		WT_ERR_MSG(session, EINVAL, "invalid name for a collator: %s", name);

	WT_ERR(__wt_calloc_one(session, &ncoll));
	WT_ERR(__wt_strdup(session, name, &ncoll->name));
	ncoll->collator = collator;

	/*将collator加入到connection中*/
	__wt_spin_lock(session, &conn->api_lock);
	TAILQ_INSERT_TAIL(&conn->collqh, ncoll, q);
	ncoll = NULL;
	__wt_spin_unlock(session, &conn->api_lock);

err:
	if (ncoll != NULL) {
		__wt_free(session, ncoll->name);
		__wt_free(session, ncoll);
	}

	API_END_RET_NOTFOUND_MAP(session, ret);
}

/*从connection中删除一个collator,外部调用接口*/
int __wt_conn_remove_collator(WT_SESSION_IMPL* session)
{
	WT_CONNECTION_IMPL *conn;
	WT_DECL_RET;
	WT_NAMED_COLLATOR *ncoll;

	conn = S2C(session);

	while ((ncoll = TAILQ_FIRST(&conn->collqh)) != NULL){
		/* Call any termination method. */
		if (ncoll->collator->terminate != NULL)
			WT_TRET(ncoll->collator->terminate(ncoll->collator, (WT_SESSION *)session));

		/* Remove from the connection's list, free memory. */
		TAILQ_REMOVE(&conn->collqh, ncoll, q);
		__wt_free(session, ncoll->name);
		__wt_free(session, ncoll);
	}

	return ret;
}

/*检查一个compressor是否加入了connection中,如果在connection当中，直接返回查找到的compressor*/
static int __compressor_confchk(WT_SESSION_IMPL* session, WT_CONFIG_ITEM* cval, WT_COMPRESSOR** compressorp)
{
	WT_CONNECTION_IMPL *conn;
	WT_NAMED_COMPRESSOR *ncomp;

	if (compressorp != NULL)
		*compressorp = NULL;

	if (cval->len == 0 || WT_STRING_MATCH("none", cval->str, cval->len))
		return (0);

	conn = S2C(session);
	TAILQ_FOREACH(ncomp, &conn->compqh, q)
		if (WT_STRING_MATCH(ncomp->name, cval->str, cval->len)) {
			if (compressorp != NULL) 
				*compressorp = ncomp->compressor;
			return (0);
		}
	WT_RET_MSG(session, EINVAL, "unknown compressor '%.*s'", (int)cval->len, cval->str);
}

int __wt_compressor_confchk(WT_SESSION_IMPL* session, WT_CONFIG_ITEM* cval)
{
	return __compressor_confchk(session, cval, NULL);
}

/*对compressor进行配置*/
int __wt_compressor_config(WT_SESSION_IMPL *session, WT_CONFIG_ITEM *cval, WT_COMPRESSOR **compressorp)
{
	return (__compressor_confchk(session, cval, compressorp));
}

/*
* __conn_add_compressor --
*	WT_CONNECTION->add_compressor method.
*/
static int __conn_add_compressor(WT_CONNECTION *wt_conn, const char *name, WT_COMPRESSOR *compressor, const char *config)
{
	WT_CONNECTION_IMPL *conn;
	WT_DECL_RET;
	WT_NAMED_COMPRESSOR *ncomp;
	WT_SESSION_IMPL *session;

	WT_UNUSED(name);
	WT_UNUSED(compressor);
	ncomp = NULL;

	conn = (WT_CONNECTION_IMPL *)wt_conn;
	CONNECTION_API_CALL(conn, session, add_compressor, config, cfg);
	WT_UNUSED(cfg);

	if (WT_STREQ(name, "none"))
		WT_ERR_MSG(session, EINVAL,
		"invalid name for a compressor: %s", name);

	WT_ERR(__wt_calloc_one(session, &ncomp));
	WT_ERR(__wt_strdup(session, name, &ncomp->name));
	ncomp->compressor = compressor;

	__wt_spin_lock(session, &conn->api_lock);
	TAILQ_INSERT_TAIL(&conn->compqh, ncomp, q);
	ncomp = NULL;
	__wt_spin_unlock(session, &conn->api_lock);

err:	if (ncomp != NULL) {
	__wt_free(session, ncomp->name);
	__wt_free(session, ncomp);
}

		API_END_RET_NOTFOUND_MAP(session, ret);
}

/*
* __wt_conn_remove_compressor --
*	remove compressor added by WT_CONNECTION->add_compressor, only used
* internally.
*/
int __wt_conn_remove_compressor(WT_SESSION_IMPL *session)
{
	WT_CONNECTION_IMPL *conn;
	WT_DECL_RET;
	WT_NAMED_COMPRESSOR *ncomp;

	conn = S2C(session);

	while ((ncomp = TAILQ_FIRST(&conn->compqh)) != NULL) {
		/* Call any termination method. */
		if (ncomp->compressor->terminate != NULL)
			WT_TRET(ncomp->compressor->terminate(ncomp->compressor, (WT_SESSION *)session));

		/* Remove from the connection's list, free memory. */
		TAILQ_REMOVE(&conn->compqh, ncomp, q);
		__wt_free(session, ncomp->name);
		__wt_free(session, ncomp);
	}

	return ret;
}

/*为connection增加一个datasource对象*/
static int __conn_add_data_source(WT_CONNECTION* wt_conn, const char *prefix, WT_DATA_SOURCE *dsrc, const char *config)
{
	WT_CONNECTION_IMPL *conn;
	WT_DECL_RET;
	WT_NAMED_DATA_SOURCE *ndsrc;
	WT_SESSION_IMPL *session;

	ndsrc = NULL;

	conn = (WT_CONNECTION_IMPL *)wt_conn;
	CONNECTION_API_CALL(conn, session, add_data_source, config, cfg);
	WT_UNUSED(cfg);

	WT_ERR(__wt_calloc_one(session, &ndsrc));
	WT_ERR(__wt_strdup(session, prefix, &ndsrc->prefix));
	ndsrc->dsrc = dsrc;

	/* Link onto the environment's list of data sources. */
	__wt_spin_lock(session, &conn->api_lock);
	TAILQ_INSERT_TAIL(&conn->dsrcqh, ndsrc, q);
	ndsrc = NULL;
	__wt_spin_unlock(session, &conn->api_lock);

err:	
	if (ndsrc != NULL) {
		__wt_free(session, ndsrc->prefix);
		__wt_free(session, ndsrc);
	}

	API_END_RET_NOTFOUND_MAP(session, ret);
}

/*向connection中添加一个extractor*/
static int __conn_add_extractor(WT_CONNECTION* wt_conn, const char* name, WT_EXTRACTOR *extractor, const char *config)
{
	WT_CONNECTION_IMPL *conn;
	WT_DECL_RET;
	WT_NAMED_EXTRACTOR *nextractor;
	WT_SESSION_IMPL *session;

	nextractor = NULL;

	conn = (WT_CONNECTION_IMPL *)wt_conn;
	CONNECTION_API_CALL(conn, session, add_extractor, config, cfg);
	WT_UNUSED(cfg);

	if (WT_STREQ(name, "none"))
		WT_ERR_MSG(session, EINVAL, "invalid name for an extractor: %s", name);

	WT_ERR(__wt_calloc_one(session, &nextractor));
	WT_ERR(__wt_strdup(session, name, &nextractor->name));
	nextractor->extractor = extractor;

	__wt_spin_lock(session, &conn->api_lock);
	TAILQ_INSERT_TAIL(&conn->extractorqh, nextractor, q);
	nextractor = NULL;
	__wt_spin_unlock(session, &conn->api_lock);

err:
	if (nextractor != NULL) {
		__wt_free(session, nextractor->name);
		__wt_free(session, nextractor);
	}

	API_END_RET_NOTFOUND_MAP(session, ret);
}

 static int __extractor_confchk(WT_SESSION_IMPL *session, WT_CONFIG_ITEM *cname, WT_EXTRACTOR **extractorp)
{
	WT_CONNECTION_IMPL *conn;
	WT_NAMED_EXTRACTOR *nextractor;

	if (extractorp != NULL)
		*extractorp = NULL;

	if (cname->len == 0 || WT_STRING_MATCH("none", cname->str, cname->len))
		return (0);

	conn = S2C(session);
	TAILQ_FOREACH(nextractor, &conn->extractorqh, q)
		if (WT_STRING_MATCH(nextractor->name, cname->str, cname->len)) {
			if (extractorp != NULL)
				*extractorp = nextractor->extractor;
			return (0);
		}
	WT_RET_MSG(session, EINVAL, "unknown extractor '%.*s'", (int)cname->len, cname->str);
}

/*
* __wt_extractor_confchk --
*	Check for a valid custom extractor (public).
*/
int __wt_extractor_confchk(WT_SESSION_IMPL *session, WT_CONFIG_ITEM *cname)
{
	return (__extractor_confchk(session, cname, NULL));
}

/*配置extractor*/
int __wt_extractor_config(WT_SESSION_IMPL *session, const char *config, WT_EXTRACTOR **extractorp, int *ownp)
{
	WT_CONFIG_ITEM cname;
	WT_EXTRACTOR *extractor;

	*extractorp = NULL;
	*ownp = 0;

	WT_RET_NOTFOUND_OK(
		__wt_config_getones_none(session, config, "extractor", &cname));
	if (cname.len == 0)
		return (0);

	WT_RET(__extractor_confchk(session, &cname, &extractor));
	if (extractor == NULL)
		return (0);

	if (extractor->customize != NULL) {
		WT_RET(__wt_config_getones(session, config, "app_metadata", &cname));
		WT_RET(extractor->customize(extractor, &session->iface, session->dhandle->name, &cname, extractorp));
	}

	if (*extractorp == NULL)
		*extractorp = extractor;
	else
		*ownp = 1;

	return 0;
}

/*从connection当中删除extractor*/
int __wt_conn_remove_extractor(WT_SESSION_IMPL *session)
{
	WT_CONNECTION_IMPL *conn;
	WT_DECL_RET;
	WT_NAMED_EXTRACTOR *nextractor;

	conn = S2C(session);

	while ((nextractor = TAILQ_FIRST(&conn->extractorqh)) != NULL) {
		/* Call any termination method. */
		if (nextractor->extractor->terminate != NULL)
			WT_TRET(nextractor->extractor->terminate(nextractor->extractor, (WT_SESSION *)session));

		/* Remove from the connection's list, free memory. */
		TAILQ_REMOVE(&conn->extractorqh, nextractor, q);
		__wt_free(session, nextractor->name);
		__wt_free(session, nextractor);
	}

	return (ret);
}

/*发起一个异步flush数据库操作并等待数据库操作完成*/
static int __conn_async_flush(WT_CONNECTION *wt_conn)
{
	WT_CONNECTION_IMPL *conn;
	WT_DECL_RET;
	WT_SESSION_IMPL *session;

	conn = (WT_CONNECTION_IMPL *)wt_conn;
	CONNECTION_API_CALL_NOCONF(conn, session, async_flush);
	WT_ERR(__wt_async_flush(session));

err:	API_END_RET_NOTFOUND_MAP(session, ret);
}

/*发起一个异步返回的数据库操作事务*/
static int __conn_async_new_op(WT_CONNECTION* wt_conn, const char* uri, const char* config, WT_ASYNC_CALLBACK* callback, WT_ASYNC_OP** asyncopp)
{
	WT_ASYNC_OP_IMPL* op;
	WT_CONNECTION_IMPL* conn;
	WT_DECL_RET;
	WT_SESSION_IMPL* session;

	conn = (WT_CONNECTION_IMPL *)wt_conn;
	CONNECTION_API_CALL(conn, session, async_new_op, config, cfg);
	WT_ERR(__wt_async_new_op(session, uri, config, cfg, callback, &op));

	*asyncopp = &op->iface;

err:
	API_END_RET_NOTFOUND_MAP(session, ret);
}

/*获取connection name的接口*/
static const char* __conn_get_home(WT_CONNECTION* wt_conn)
{
	return (((WT_CONNECTION_IMPL*)wt_conn)->home);
}

/*更新method的配置信息*/
static int __conn_configure_method(WT_CONNECTION* wt_conn, const char* method, const char* uri, const char* config, const char* type, const char* check)
{
	WT_CONNECTION_IMPL *conn;
	WT_DECL_RET;
	WT_SESSION_IMPL *session;

	conn = (WT_CONNECTION_IMPL *)wt_conn;
	CONNECTION_API_CALL_NOCONF(conn, session, configure_method);

	ret = __wt_configure_method(session, method, uri, config, type, check);
err:
	API_END_RET_NOTFOUND_MAP(session, ret);
}

static int __conn_is_new(WT_CONNECTION* wt_conn)
{
	return (((WT_CONNECTION_IMPL *)wt_conn)->is_new);
}

static int __conn_close(WT_CONNECTION* wt_conn, const char* config)
{
	WT_CONFIG_ITEM* cval;
	WT_CONNECTION_IMPL* conn;
	WT_DECL_RET;
	WT_SESSION* wt_session;
	WT_SESSION_IMPL* s, *session;
	uint32_t i;

	conn = (WT_CONNECTION_IMPL*)wt_conn;
	CONNECTION_API_CALL(conn, session, close, config, cfg);

	WT_TRET(__wt_config_gets(session, cfg, "leak_memory", &cval));
	if (cval.val != 0)
		F_SET(conn, WT_CONN_LEAK_MEMORY);

err:
	/*关闭connection之前，先回滚所有正在connection运行的事务*/
	for (s = conn->sessions, i = 0; i < conn->session_cnt; ++s, ++i){
		if (s->active && !F_ISSET(s, WT_SESSION_INTERNAL) &&
			F_ISSET(&s->txn, TXN_RUNNING)) {
			wt_session = &s->iface;
			WT_TRET(wt_session->rollback_transaction(wt_session, NULL));
		}
	}
	/*关闭所有外部创建的session*/
	for (s = conn->sessions, i = 0; i < conn->session_cnt; ++s, ++i){
		if (s->active && !F_ISSET(s, WT_SESSION_INTERNAL)) {
			wt_session = &s->iface;
			/*
			* Notify the user that we are closing the session
			* handle via the registered close callback.
			*/
			if (s->event_handler->handle_close != NULL)
				WT_TRET(s->event_handler->handle_close(s->event_handler, wt_session, NULL));
			WT_TRET(wt_session->close(wt_session, config));
		}
	}

	WT_TRET(__wt_connection_close(conn));

	session = NULL;

	API_END_RET_NOTFOUND_MAP(session, ret);
}

/*对connection进行重配置*/
static int __conn_reconfigure(WT_CONNECTION* wt_conn, const char* config)
{
	WT_CONNECTION_IMPL *conn;
	WT_DECL_RET;
	WT_SESSION_IMPL *session;
	const char *p, *config_cfg[] = { NULL, NULL, NULL };

	conn = (WT_CONNECTION_IMPL*)wt_conn;
	CONNECTION_API_CALL(conn, session, reconfigure, config, cfg);

	/* Serialize reconfiguration. */
	__wt_spin_lock(session, &conn->reconfig_lock);
	/*
	* The configuration argument has been checked for validity, replace the
	* previous connection configuration.
	*
	* DO NOT merge the configuration before the reconfigure calls.  Some
	* of the underlying reconfiguration functions do explicit checks with
	* the second element of the configuration array, knowing the defaults
	* are in slot #1 and the application's modifications are in slot #2.
	*/
	config_cfg[0] = conn->cfg;
	config_cfg[1] = config;

	WT_ERR(__conn_statistics_config(session, config_cfg));
	WT_ERR(__wt_async_reconfig(session, config_cfg));
	WT_ERR(__wt_cache_config(session, 1, config_cfg));
	WT_ERR(__wt_checkpoint_server_create(session, config_cfg));
	WT_ERR(__wt_lsm_manager_reconfig(session, config_cfg));
	WT_ERR(__wt_statlog_create(session, config_cfg));
	WT_ERR(__wt_sweep_config(session, cfg));
	WT_ERR(__wt_verbose_config(session, config_cfg));

	WT_ERR(__wt_config_merge(session, config_cfg, &p));
	__wt_free(session, conn->cfg);
	conn->cfg = p;

err:
	__wt_spin_unlock(session, &conn->reconfig_lock);
	API_END_RET(session, ret);
}

/*打开一个外部session*/
static int __conn_open_session(WT_CONNECTION* wt_conn, WT_EVENT_HANDLER* event_handler, const char* config, WT_SESSION** wt_sessionp)
{
	WT_CONNECTION_IMPL *conn;
	WT_DECL_RET;
	WT_SESSION_IMPL *session, *session_ret;

	*wt_sessionp = NULL;

	conn = (WT_CONNECTION_IMPL *)wt_conn;
	session_ret = NULL;

	CONNECTION_API_CALL(conn, session, open_session, config, cfg);
	WT_UNUSED(cfg);

	WT_ERR(__wt_open_session(conn, event_handler, config, &session_ret));

	*wt_sessionp = &session_ret->iface;

err:
	API_END_RET_NOTFOUND_MAP(session, ret);
}

/*向配置项队列中加入一个config*/
static void __conn_config_append(const char* cfg[], const char* config)
{
	while (*cfg != NULL)
		++cfg;
	*cfg = config;
}

/*判断config是否与wiredtiger引擎版本兼容*/
static int __conn_config_check_version(WT_SESSION_IMPL* session, const char* config)
{
	WT_CONFIG_ITEM vmajor, vminor;

	/*
	* Version numbers aren't included in all configuration strings, but  we check all of them just in case. Ignore configurations without a version.
	*/
	if (__wt_config_getones(session, config, "version.major", &vmajor) == WT_NOTFOUND)
		return 0;
	WT_RET(__wt_config_getones(session, config, "version.minor", &vminor));

	if (vmajor.val > WIREDTIGER_VERSION_MAJOR || (vmajor.val == WIREDTIGER_VERSION_MAJOR && vminor.val > WIREDTIGER_VERSION_MINOR))
		WT_RET_MSG(session, ENOTSUP, "WiredTiger configuration is from an incompatible release of the WiredTiger engine");

	return 0;
}

/*从home目录读取wiredtiger的配置文件信息*/
static int __conn_config_file(WT_SESSION_IMPL* session, const char* filename, int is_user, const char** cfg, WT_ITEM *cbuf)
{
	WT_DECL_RET;
	WT_FH *fh;
	size_t len;
	wt_off_t size;
	int exist, quoted;
	char *p, *t;

	fh = NULL;

	/* Configuration files are always optional. */
	WT_RET(__wt_exist(session, filename, &exist));
	if (!exist)
		return (0);

	/* Open the configuration file. */
	WT_RET(__wt_open(session, filename, 0, 0, 0, &fh));
	WT_ERR(__wt_filesize(session, fh, &size));
	if (size == 0)
		goto err;

	/*配置文件不会大于100KB,太大了就抛出一个错误*/
	if (size > 100 * 1024)
		WT_ERR_MSG(session, EFBIG, "Configuration file too big: %s", filename);
	len = (size_t)size;

	/*将文件内容读取到内存中*/
	WT_ERR(__wt_buf_init(session, cbuf, len + 10));
	WT_ERR(__wt_read(session, fh, (wt_off_t)0, len, ((uint8_t *)cbuf->mem) + 1));
	((uint8_t *)cbuf->mem)[0] = '\n';
	cbuf->size = len + 1;

	/*
	* Collapse the file's lines into a single string: newline characters
	* are replaced with commas unless the newline is quoted or backslash
	* escaped.  Comment lines (an unescaped newline where the next non-
	* white-space character is a hash), are discarded.
	*/
	for (quoted = 0, p = t = cbuf->mem; len > 0;) {
		/*
		* Backslash pairs pass through untouched, unless immediately
		* preceding a newline, in which case both the backslash and
		* the newline are discarded.  Backslash characters escape
		* quoted characters, too, that is, a backslash followed by a
		* quote doesn't start or end a quoted string.
		*/
		if (*p == '\\' && len > 1) {
			if (p[1] != '\n') {
				*t++ = p[0];
				*t++ = p[1];
			}
			p += 2;
			len -= 2;
			continue;
		}

		/*
		* If we're in a quoted string, or starting a quoted string,
		* take all characters, including white-space and newlines.
		*/
		if (quoted || *p == '"') {
			if (*p == '"')
				quoted = !quoted;
			*t++ = *p++;
			--len;
			continue;
		}

		/* Everything else gets taken, except for newline characters. */
		if (*p != '\n') {
			*t++ = *p++;
			--len;
			continue;
		}

		/*
		* Replace any newline characters with commas (and strings of
		* commas are safe).
		*
		* After any newline, skip to a non-white-space character; if
		* the next character is a hash mark, skip to the next newline.
		*/
		for (;;) {
			for (*t++ = ','; --len > 0 && isspace(*++p);)
				;
			if (len == 0)
				break;
			if (*p != '#')
				break;
			while (--len > 0 && *++p != '\n')
				;
			if (len == 0)
				break;
		}
	}
	*t = '\0';
	cbuf->size = WT_PTRDIFF(t, cbuf->data);

	WT_ERR(__conn_config_check_version(session, cbuf->data));

	/* Upgrade the configuration string. */
	WT_ERR(__wt_config_upgrade(session, cbuf));

	/* Check the configuration information. 判断配置的合法性 */
	WT_ERR(__wt_config_check(session, is_user ? WT_CONFIG_REF(session, wiredtiger_open_usercfg) : WT_CONFIG_REF(session, wiredtiger_open_basecfg), cbuf->data, 0));
	/*将配置追加到config stack中*/
	__conn_config_append(cfg, cbuf->data);

err:
	WT_TRET(__wt_close(session, &fh));
}

/*WIREDTIGER_CONFIG环境变量下读取配置信息,并加入到config stack中*/
static int __conn_config_env(WT_SESSION_IMPL *session, const char *cfg[], WT_ITEM *cbuf)
{
	WT_CONFIG_ITEM cval;
	WT_DECL_RET;
	const char *env_config;
	size_t len;

	ret = __wt_getenv(session, "WIREDTIGER_CONFIG", &env_config);
	if (ret == WT_NOTFOUND)
		return 0;
	WT_ERR(ret);

	len = strlen(env_config);
	if (len == 0)
		goto err;			/* Free the memory. */
	WT_ERR(__wt_buf_set(session, cbuf, env_config, len + 1));

	/*
	* Security stuff:
	*
	* If the "use_environment_priv" configuration string is set, use the
	* environment variable if the process has appropriate privileges.
	*/
	WT_ERR(__wt_config_gets(session, cfg, "use_environment_priv", &cval));
	if (cval.val == 0 && __wt_has_priv())
		WT_ERR_MSG(session, WT_ERROR, "%s", "WIREDTIGER_CONFIG environment variable set but process lacks privileges to use that environment variable");

	/* Check any version. */
	WT_ERR(__conn_config_check_version(session, env_config));

	/* Upgrade the configuration string. */
	WT_ERR(__wt_config_upgrade(session, cbuf));

	/* Check the configuration information. */
	WT_ERR(__wt_config_check(session, WT_CONFIG_REF(session, wiredtiger_open), env_config, 0));

	/* Append it to the stack. */
	__conn_config_append(cfg, cbuf->data);

err:	__wt_free(session, env_config);

	return ret;
}

/*设置使用wiredtiger引擎的工作路径*/
static int __conn_home(WT_SESSION_IMPL* session, const char* home, const char* cfg[])
{
	WT_DECL_RET;
	WT_CONFIG_ITEM cval;

	/* If the application specifies a home directory, use it. */
	if (home != NULL)
		goto copy;

	ret = __wt_getenv(session, "WIREDTIGER_HOME", &S2C(session)->home);
	if (ret == 0)
		return 0;

	home = ".";

	WT_RET(__wt_config_gets(session, cfg, "use_environment_priv", &cval));
	if (cval.val == 0 && __wt_has_priv())
		WT_RET_MSG(session, WT_ERROR, "%s",
		"WIREDTIGER_HOME environment variable set but process lacks privileges to use that environment variable");

copy:
	return __wt_strdup(session, home, &S2C(session)->home);
}

#define	WT_SINGLETHREAD_STRING	"WiredTiger lock file\n"
static int __conn_single(WT_SESSION_IMPL* session, const char* cfg[])
{
	WT_CONFIG_ITEM cval;
	WT_CONNECTION_IMPL *conn, *t;
	WT_DECL_RET;
	WT_FH *fh;
	size_t len;
	wt_off_t size;
	char buf[256];

	conn = S2C(session);
	fh = NULL;

	__wt_spin_lock(session, &__wt_process.spinlock);

	TAILQ_FOREACH(t, &__wt_process.connqh, q){
		if (t->home != NULL && t != conn && strcmp(t->home, conn->home) == 0){
			ret = EBUSY;
			break;
		}
	}

	if (ret != 0)
		WT_ERR_MSG(session, EBUSY, "WiredTiger database is already being managed by another thread in this process");

	/*
	* !!!
	* Be careful changing this code.
	*
	* We locked the WiredTiger file before release 2.3.2; a separate lock
	* file was added after 2.3.1 because hot backup has to copy the
	* WiredTiger file and system utilities on Windows can't copy locked
	* files.
	*
	* For this reason, we don't use the lock file's existence to decide if
	* we're creating the database or not, use the WiredTiger file instead,
	* it has existed in every version of WiredTiger.
	*
	* Additionally, avoid an upgrade race: a 2.3.1 release process might
	* have the WiredTiger file locked, and we're going to create the lock
	* file and lock it instead. For this reason, first acquire a lock on
	* the lock file and then a lock on the WiredTiger file, then release
	* the latter so hot backups can proceed.  (If someone were to run a
	* current release and subsequently a historic release, we could still
	* fail because the historic release will ignore our lock file and will
	* then successfully lock the WiredTiger file, but I can't think of any
	* way to fix that.)
	*
	* Open the WiredTiger lock file, creating it if it doesn't exist. (I'm
	* not removing the lock file if we create it and subsequently fail, it
	* isn't simple to detect that case, and there's no risk other than a
	* useless file being left in the directory.)
	*/
	WT_ERR(__wt_open(session, WT_SINGLETHREAD, 1, 0, 0, &conn->lock_fh));

	if (__wt_bytelock(conn->lock_fh, (wt_off_t)0, 1) != 0)
		WT_ERR_MSG(session, EBUSY, "WiredTiger database is already being managed by another process");

	WT_ERR(__wt_filesize(session, conn->lock_fh, &size));
	if (size == 0){ /**写入一个file lock*/
		WT_ERR(__wt_write(session, conn->lock_fh, (wt_off_t)0, strlen(WT_SINGLETHREAD_STRING), WT_SINGLETHREAD_STRING));
	}
	/* We own the lock file, optionally create the WiredTiger file. */
	WT_ERR(__wt_config_gets(session, cfg, "create", &cval));
	WT_ERR(__wt_open(session, WT_WIREDTIGER, cval.val == 0 ? 0 : 1, 0, 0, &fh));

	/*
	* Lock the WiredTiger file (for backward compatibility reasons as
	* described above).  Immediately release the lock, it's just a test.
	*/
	if (__wt_bytelock(fh, (wt_off_t)0, 1) != 0) {
		WT_ERR_MSG(session, EBUSY, "WiredTiger database is already being managed by another process");
	}
	WT_ERR(__wt_bytelock(fh, (wt_off_t)0, 0));
	/*写入一个头信息到WiredTiger文件中*/
	WT_ERR(__wt_filesize(session, fh, &size));
	if (size == 0) { /*写入一个信息标示有进程使用wiretiger数据库*/
		len = (size_t)snprintf(buf, sizeof(buf), "%s\n%s\n", WT_WIREDTIGER, WIREDTIGER_VERSION_STRING);
		WT_ERR(__wt_write(session, fh, (wt_off_t)0, len, buf));
		WT_ERR(__wt_fsync(session, fh));

		conn->is_new = 1;
	}
	else {/*引擎需要独占整个库，那么不允许其他进程进行访问，这个时候已经有其他进程启动了，只能进行错误提示*/
		WT_ERR(__wt_config_gets(session, cfg, "exclusive", &cval));
		if (cval.val != 0)
			WT_ERR_MSG(session, EEXIST, "WiredTiger database already exists and exclusive option configured");
		conn->is_new = 0;
	}

err:
	WT_TRET(__wt_close(session, &fh));
	__wt_spin_unlock(session, &__wt_process.spinlock);
	return ret;
}

/*设置统计模块配置*/
static int __conn_statistics_config(WT_SESSION_IMPL* session, const char* cfg[])
{
	WT_CONFIG_ITEM cval, sval;
	WT_CONNECTION_IMPL *conn;
	WT_DECL_RET;
	uint32_t flags;
	int set;

	conn = S2C(session);

	WT_RET(__wt_config_gets(session, cfg, "statistics", &cval));

	flags = 0;
	set = 0;
	if ((ret = __wt_config_subgets(session, &cval, "none", &sval)) == 0 && sval.val != 0) {
		LF_SET(WT_CONN_STAT_NONE);
		++set;
	}
	WT_RET_NOTFOUND_OK(ret);

	if ((ret = __wt_config_subgets(session, &cval, "fast", &sval)) == 0 && sval.val != 0) {
		LF_SET(WT_CONN_STAT_FAST);
		++set;
	}
	WT_RET_NOTFOUND_OK(ret);

	if ((ret = __wt_config_subgets(session, &cval, "all", &sval)) == 0 && sval.val != 0) {
		LF_SET(WT_CONN_STAT_ALL | WT_CONN_STAT_FAST);
		++set;
	}
	WT_RET_NOTFOUND_OK(ret);

	if ((ret = __wt_config_subgets(session, &cval, "clear", &sval)) == 0 && sval.val != 0)
		LF_SET(WT_CONN_STAT_CLEAR);
	WT_RET_NOTFOUND_OK(ret);

	if (set > 1)
		WT_RET_MSG(session, EINVAL, "only one statistics configuration value may be specified");

	/* Configuring statistics clears any existing values. */
	conn->stat_flags = flags;

	return 0;
}

typedef struct
{
	const char* name;
	uint32_t flag;
}WT_NAME_FLAG;

/*对verbose的配置*/
int __wt_verbose_config(WT_SESSION_IMPL *session, const char *cfg[])
{
	static const WT_NAME_FLAG verbtypes[] = {
		{ "api", WT_VERB_API },
		{ "block", WT_VERB_BLOCK },
		{ "checkpoint", WT_VERB_CHECKPOINT },
		{ "compact", WT_VERB_COMPACT },
		{ "evict", WT_VERB_EVICT },
		{ "evictserver", WT_VERB_EVICTSERVER },
		{ "fileops", WT_VERB_FILEOPS },
		{ "log", WT_VERB_LOG },
		{ "lsm", WT_VERB_LSM },
		{ "metadata", WT_VERB_METADATA },
		{ "mutex", WT_VERB_MUTEX },
		{ "overflow", WT_VERB_OVERFLOW },
		{ "read", WT_VERB_READ },
		{ "reconcile", WT_VERB_RECONCILE },
		{ "recovery", WT_VERB_RECOVERY },
		{ "salvage", WT_VERB_SALVAGE },
		{ "shared_cache", WT_VERB_SHARED_CACHE },
		{ "split", WT_VERB_SPLIT },
		{ "temporary", WT_VERB_TEMPORARY },
		{ "transaction", WT_VERB_TRANSACTION },
		{ "verify", WT_VERB_VERIFY },
		{ "version", WT_VERB_VERSION },
		{ "write", WT_VERB_WRITE },
		{ NULL, 0 }
	};
	WT_CONFIG_ITEM cval, sval;
	WT_CONNECTION_IMPL *conn;
	WT_DECL_RET;
	const WT_NAME_FLAG *ft;
	uint32_t flags;

	conn = S2C(session);

	WT_RET(__wt_config_gets(session, cfg, "verbose", &cval));

	flags = 0;
	for (ft = verbtypes; ft->name != NULL; ft++) {
		if ((ret = __wt_config_subgets(session, &cval, ft->name, &sval)) == 0 && sval.val != 0) {
#ifdef HAVE_VERBOSE
			LF_SET(ft->flag);
#else
			WT_RET_MSG(session, EINVAL,
				"Verbose option specified when WiredTiger built "
				"without verbose support. Add --enable-verbose to "
				"configure command and rebuild to include support "
				"for verbose messages");
#endif
		}
		WT_RET_NOTFOUND_OK(ret);
	}

	conn->verbose = flags;
	return 0;
}

/*创建数据库时保存基本的配置项*/
static int __conn_write_base_config(WT_SESSION_IMPL* session, const char* cfg[])
{
	FILE *fp;
	WT_CONFIG parser;
	WT_CONFIG_ITEM cval, k, v;
	WT_DECL_RET;
	int exist;

	fp = NULL;

	/*
	* Discard any base configuration setup file left-over from previous
	* runs.  This doesn't matter for correctness, it's just cleaning up
	* random files.
	*/
	WT_RET(__wt_remove_if_exists(session, WT_BASECONFIG_SET));

	/* The base configuration file is optional, check the configuration. */
	WT_RET(__wt_config_gets(session, cfg, "config_base", &cval));
	if (!cval.val)
		return (0);

	/*
	* We don't test separately if we're creating the database in this run
	* as we might have crashed between creating the "WiredTiger" file and
	* creating the base configuration file. If configured, there's always
	* a base configuration file, and we rename it into place, so it can
	* only NOT exist if we crashed before it was created; in other words,
	* if the base configuration file exists, we're done.
	*/
	WT_RET(__wt_exist(session, WT_BASECONFIG, &exist));
	if (exist)
		return (0);

	WT_RET(__wt_fopen(session, WT_BASECONFIG_SET, WT_FHANDLE_WRITE, 0, &fp));

	fprintf(fp, "%s\n\n",
		"# Do not modify this file.\n"
		"#\n"
		"# WiredTiger created this file when the database was created,\n"
		"# to store persistent database settings.  Instead of changing\n"
		"# these settings, set a WIREDTIGER_CONFIG environment variable\n"
		"# or create a WiredTiger.config file to override them.");

	fprintf(fp, "version=(major=%d,minor=%d)\n\n", WIREDTIGER_VERSION_MAJOR, WIREDTIGER_VERSION_MINOR);

	/*
	* We were passed an array of configuration strings where slot 0 is all
	* possible values and the second and subsequent slots are changes
	* specified by the application during open (using the wiredtiger_open
	* configuration string, an environment variable, or user-configuration
	* file). The base configuration file contains all changes to default
	* settings made at create, and we include the user-configuration file
	* in that list, even though we don't expect it to change. Of course,
	* an application could leave that file as it is right now and not
	* remove a configuration we need, but applications can also guarantee
	* all database users specify consistent environment variables and
	* wiredtiger_open configuration arguments, and if we protect against
	* those problems, might as well include the application's configuration
	* file as well.
	*
	* We want the list of defaults that have been changed, that is, if the
	* application didn't somehow configure a setting, we don't write out a
	* default value, so future releases may silently migrate to new default
	* values.
	*/
	while (*++cfg != NULL) {
		WT_ERR(__wt_config_init(session, &parser, WT_CONFIG_BASE(session, wiredtiger_open_basecfg)));
		while ((ret = __wt_config_next(&parser, &k, &v)) == 0) {
			if ((ret = __wt_config_getone(session, *cfg, &k, &v)) == 0) {
				/* Fix quoting for non-trivial settings. */
				if (v.type == WT_CONFIG_ITEM_STRING) {
					--v.str;
					v.len += 2;
				}
				fprintf(fp, "%.*s=%.*s\n", (int)k.len, k.str, (int)v.len, v.str);
			}
			WT_ERR_NOTFOUND_OK(ret);
		}
		WT_ERR_NOTFOUND_OK(ret);
	}

	/* Flush the handle and rename the file into place. */
	return (__wt_sync_and_rename_fp(session, &fp, WT_BASECONFIG_SET, WT_BASECONFIG));

	/* Close any file handle left open, remove any temporary file. */
err:	
	WT_TRET(__wt_fclose(&fp, WT_FHANDLE_WRITE));
	WT_TRET(__wt_remove_if_exists(session, WT_BASECONFIG_SET));

	return (ret);
}

/*外部打开wiredtiger数据库connection*/
int __wiredtiger_open(const char *home, WT_EVENT_HANDLER *event_handler, const char *config, WT_CONNECTION **wt_connp)
{
	static const WT_CONNECTION stdc = {
		__conn_async_flush,
		__conn_async_new_op,
		__conn_close,
		__conn_reconfigure,
		__conn_get_home,
		__conn_configure_method,
		__conn_is_new,
		__conn_open_session,
		__conn_load_extension,
		__conn_add_data_source,
		__conn_add_collator,
		__conn_add_compressor,
		__conn_add_extractor,
		__conn_get_extension_api
	};

	static const WT_NAME_FLAG file_types[] = {
		{ "checkpoint", WT_FILE_TYPE_CHECKPOINT },
		{ "data", WT_FILE_TYPE_DATA },
		{ "log", WT_FILE_TYPE_LOG },
		{ NULL, 0 }
	};

	WT_CONFIG_ITEM cval, sval;
	WT_CONNECTION_IMPL *conn;
	WT_DECL_ITEM(i1);
	WT_DECL_ITEM(i2);
	WT_DECL_ITEM(i3);
	WT_DECL_RET;
	const WT_NAME_FLAG *ft;
	WT_SESSION_IMPL *session;

	const char* cfg[] = {NULL, NULL, NULL, NULL, NULL, NULL};
	*wt_connp = NULL;

	conn = NULL;
	session = NULL;

	/*初始化wiredtiger库*/
	WT_RET(__wt_library_init());

	WT_RET(__wt_calloc_one(NULL, &conn));
	conn->iface = stdc;

	/*
	* Immediately link the structure into the connection structure list:
	* the only thing ever looked at on that list is the database name,
	* and a NULL value is fine.
	*/
	__wt_spin_lock(NULL, &__wt_process.spinlock);
	TAILQ_INSERT_TAIL(&__wt_process.connqh, conn, q);
	__wt_spin_unlock(NULL, &__wt_process.spinlock);

	session = conn->default_session = &conn->dummy_session;
	session->iface.connection = &conn->iface;
	session->name = "wiredtiger_open";
	__wt_random_init(session->rnd);
	__wt_event_handler_set(session, event_handler);

	WT_ERR(__wt_connection_init(conn));

	/* Check/set the application-specified configuration string. */
	WT_ERR(__wt_config_check(session, WT_CONFIG_REF(session, wiredtiger_open), config, 0));
	cfg[0] = WT_CONFIG_BASE(session, wiredtiger_open);
	cfg[1] = config;

	/* Configure error messages so we get them right early. */
	WT_ERR(__wt_config_gets(session, cfg, "error_prefix", &cval));
	if (cval.len != 0)
		WT_ERR(__wt_strndup(session, cval.str, cval.len, &conn->error_prefix));

	WT_ERR(__conn_home(session, home, cfg));

	/*确定是否有其他的进程(线程)在使用这个数据库*/
	WT_ERR(__conn_single(session, cfg));

	/*
	* Build the configuration stack, in the following order (where later
	* entries override earlier entries):
	*
	* 1. all possible wiredtiger_open configurations
	* 2. base configuration file, created with the database (optional)
	* 3. the config passed in by the application.
	* 4. user configuration file (optional)
	* 5. environment variable settings (optional)
	*
	* Clear the entries we added to the stack, we're going to build it in
	* order.
	*/
	WT_ERR(__wt_scr_alloc(session, 0, &i1));
	WT_ERR(__wt_scr_alloc(session, 0, &i2));
	WT_ERR(__wt_scr_alloc(session, 0, &i3));
	cfg[0] = WT_CONFIG_BASE(session, wiredtiger_open_all);
	cfg[1] = NULL;
	WT_ERR(__conn_config_file(session, WT_BASECONFIG, 0, cfg, i1));
	__conn_config_append(cfg, config);
	WT_ERR(__conn_config_file(session, WT_USERCONFIG, 1, cfg, i2));
	WT_ERR(__conn_config_env(session, cfg, i3));

	/*对connection进行配置*/
	WT_ERR(__wt_config_gets(session, cfg, "error_prefix", &cval));
	if (cval.len != 0) {
		__wt_free(session, conn->error_prefix);
		WT_ERR(__wt_strndup(session, cval.str, cval.len, &conn->error_prefix));
	}
	/*确定hazard pointer队列的长度*/
	WT_ERR(__wt_config_gets(session, cfg, "hazard_max", &cval));
	conn->hazard_max = (uint32_t)cval.val;
	/*确定session slot的最大个数*/
	WT_ERR(__wt_config_gets(session, cfg, "session_max", &cval));
	conn->session_size = (uint32_t)cval.val + WT_NUM_INTERNAL_SESSIONS;

	WT_ERR(__wt_config_gets(session, cfg, "session_scratch_max", &cval));
	conn->session_scratch_max = (size_t)cval.val;

	WT_ERR(__wt_config_gets(session, cfg, "checkpoint_sync", &cval));
	if (cval.val)
		F_SET(conn, WT_CONN_CKPT_SYNC);

	WT_ERR(__wt_config_gets(session, cfg, "direct_io", &cval));
	for (ft = file_types; ft->name != NULL; ft++) {
		ret = __wt_config_subgets(session, &cval, ft->name, &sval);
		if (ret == 0) {
			if (sval.val)
				FLD_SET(conn->direct_io, ft->flag);
		}
		else if (ret != WT_NOTFOUND)
			goto err;
	}

	/*默认direct io是4K大小对齐，可以有上层配置*/
	WT_ERR(__wt_config_gets(session, cfg, "buffer_alignment", &cval));
	if (cval.val == -1)
		conn->buffer_alignment = (conn->direct_io == 0) ? 0 : WT_BUFFER_ALIGNMENT_DEFAULT;
	else
		conn->buffer_alignment = (size_t)cval.val;

#ifndef HAVE_POSIX_MEMALIGN
	if (conn->buffer_alignment != 0)
		WT_ERR_MSG(session, EINVAL, "buffer_alignment requires posix_memalign");
#endif

	WT_ERR(__wt_config_gets(session, cfg, "file_extend", &cval));
	for (ft = file_types; ft->name != NULL; ft++) {
		ret = __wt_config_subgets(session, &cval, ft->name, &sval);
		if (ret == 0) {
			switch (ft->flag) {
			case WT_FILE_TYPE_DATA:
				conn->data_extend_len = sval.val;
				break;
			case WT_FILE_TYPE_LOG:
				conn->log_extend_len = sval.val;
				break;
			}
		}
		else if (ret != WT_NOTFOUND)
			goto err;
	}

	WT_ERR(__wt_config_gets(session, cfg, "mmap", &cval));
	conn->mmap = cval.val == 0 ? 0 : 1;

	WT_ERR(__conn_statistics_config(session, cfg));
	WT_ERR(__wt_lsm_manager_config(session, cfg));
	WT_ERR(__wt_sweep_config(session, cfg));
	WT_ERR(__wt_verbose_config(session, cfg));

	/* Now that we know if verbose is configured, output the version. */
	WT_ERR(__wt_verbose(session, WT_VERB_VERSION, "%s", WIREDTIGER_VERSION_STRING));

	/*
	* Open the connection, then reset the local session as the real one
	* was allocated in __wt_connection_open.
	* 初始化并打开connection
	*/
	WT_ERR(__wt_connection_open(conn, cfg));
	session = conn->default_session;

	/*
	* Check on the turtle and metadata files, creating them if necessary
	* (which avoids application threads racing to create the metadata file
	* later).  Once the metadata file exists, get a reference to it in
	* the connection's session.
	* 初始化元数据文件
	*/
	WT_ERR(__wt_turtle_init(session));
	WT_ERR(__wt_metadata_open(session));
	/*加载扩展模块*/
	WT_ERR(__conn_load_extensions(session, cfg));
	/*配置完成，将基础的配置信息写入到WiredTiger.basecfg中*/
	WT_ERR(__conn_write_base_config(session, cfg));
	/*Start the worker threads last.启动connection对应的工作线程*/
	WT_ERR(__wt_connection_workers(session, cfg));

	/* Merge the final configuration for later reconfiguration. */
	WT_ERR(__wt_config_merge(session, cfg, &conn->cfg));

	WT_STATIC_ASSERT(offsetof(WT_CONNECTION_IMPL, iface) == 0);
	*wt_connp = &conn->iface;

err:	
	/* Discard the scratch buffers. */
	__wt_scr_free(session, &i1);
	__wt_scr_free(session, &i2);
	__wt_scr_free(session, &i3);

	/*
	* We may have allocated scratch memory when using the dummy session or
	* the subsequently created real session, and we don't want to tie down
	* memory for the rest of the run in either of them.
	*/
	if (session != &conn->dummy_session)
		__wt_scr_discard(session);
	__wt_scr_discard(&conn->dummy_session);

	if (ret != 0)
		WT_TRET(__wt_connection_close(conn));

	return (ret);
}
















