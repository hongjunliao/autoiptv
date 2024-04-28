/*!
 * This file is PART of autoiptv project
 * @author hongjun.liao <docici@126.com>, @date 2024/4/10
 *
 * 使用--urls中的URL，通过关键字匹配，检测并更新--m3u中的URL
 * autoiptv --m3u cctv.m3u --url url.txt
 *
 * 更新前的cctv.m3u:
 *
 * #EXTM3U x-tvg-url="https://...
 * #EXTINF:-1 tvg-id="A01" tvg-name="CCTV1" tvg-logo="https://cdn.jsdelivr.net/gh/wanglindl/TVlogo@main/img/CCTV1.png" group-title="央视",CCTV-1 综合
 * http://[2409:8087:7000:20:1000::22]:6060/yinhe/2/ch00000090990000001331/index.m3u8?virtualDomain=yinhe.live_hls.zte.com
 * #EXTINF:-1 tvg-id="A02" tvg-name="CCTV2" tvg-logo="https://cdn.jsdelivr.net/gh/wanglindl/TVlogo@main/img/CCTV2.png" group-title="央视",CCTV-2 财经
 * http://[2409:8087:7000:20:1000::22]:6060/yinhe/2/ch00000090990000001332/index.m3u8?virtualDomain=yinhe.live_hls.zte.com
 *
 * url.txt内容:
 *
 * https://gitee.com/happy-is-not-closed/IPTV/raw/main/IPTV.m3u
 * https://iptv-org.github.io/iptv/countries/cn.m3u
 *
 * 更新后的cctv.m3u:
 *
 * #EXTM3U x-tvg-url="https://...
 * #EXTINF:-1 tvg-id="A01" tvg-name="CCTV1" tvg-logo="https://cdn.jsdelivr.net/gh/wanglindl/TVlogo@main/img/CCTV1.png" group-title="央视",CCTV-1 综合
 * http://39.134.24.162/dbiptv.sn.chinamobile.com/PLTV/88888890/224/3221225804/index.m3u8
 * #EXTINF:-1 tvg-id="A02" tvg-name="CCTV2" tvg-logo="https://cdn.jsdelivr.net/gh/wanglindl/TVlogo@main/img/CCTV2.png" group-title="央视",CCTV-2 财经
 * http://39.134.24.162/dbiptv.sn.chinamobile.com/PLTV/88888890/224/3221226195/index.m3u8
 *
 */
/////////////////////////////////////////////////////////////////////////////////////////

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include <iostream>
#include <string.h>
#include <assert.h>
#include "hp/sdsinc.h"	//sds
#include "hp/hp_log.h"	//hp_log
#include "hp/hp_tuple.h"	//hp_tuple
#include "hp/hp_stdlib.h"	//
#include "hp/hp_curl.h"	//
#include "hp/hp_dict.h"	//
#include "hp/hp_search.h"	//
#include "libyuarel/yuarel.h"	//yuarel_parse
#include <getopt.h>		/* getopt_long */
#include "hp/string_util.h"	//hp_fread
#include <curl/curl.h>   /* libcurl */
#ifdef HAVE_REGEX_H
#include <regex.h>
#else
#endif /* HAVE_REGEX_H */

extern "C" {
int curl_https_main(void);
int autoiptv_main(int argc, char **argv);
}

/////////////////////////////////////////////////////////////////////////////////////////
typedef struct tvg_st tvg_st;

//频道信息
struct tvg_st{
	sds desc;     //描述信息
	sds extinf;   //EXTINF:
	sds k;        //关键字
	//m3u URL,包含现有的,及从.m3u文件中查找到的
	//添加'\n'到字串符串结尾以标记为已更新,第1个元素为主URL,如果已更新,则视为该频道已完成更新
	sds urls[8];
	int iurls, rurls;    //
};

typedef struct m3u_download_ctx {
	sds m3us[128];	//每个元素包含一个m3u文件的全部内容
	int n_m3us;
	int done;
} m3u_download_ctx;

typedef struct m3u_chk_ctx {
	tvg_st tvgs[20000];
	int n_tvg, n_resp, n_req;
} m3u_chk_ctx;
/////////////////////////////////////////////////////////////////////////////////////////
static 	m3u_download_ctx dctxobj = { 0 }, * g_dctx = &dctxobj;
static 	m3u_chk_ctx g_chkctxobj = {0}, * g_chkctx = &g_chkctxobj;

/////////////////////////////////////////////////////////////////////////////////////////
static sds regex_find(char const *buf, char const * pattern, int * so, int * eo)
{
	assert(buf && pattern);
	int rc;
	sds k = 0;
#ifdef HAVE_REGEX_H
		regex_t start_state;
		regmatch_t matchptr[2] = { 0 };
		rc = regcomp(&start_state, pattern, REG_EXTENDED | REG_ICASE);
		if(!(rc == 0)) return sdsempty();

		int status = regexec(&start_state, buf + (eo? *eo : 0), sizeof(matchptr) / sizeof(matchptr[0]), matchptr, 0);
		regfree(&start_state);
		if(status == 0 && matchptr[1].rm_eo - matchptr[1].rm_so > 0){
			k = sdsnewlen(buf + matchptr[1].rm_so, matchptr[1].rm_eo - matchptr[1].rm_so);
			if(so) *so = matchptr[1].rm_so;
			if(eo) *eo = matchptr[1].rm_eo;
		}
#else
		int i, nlines = 0;
		sds * lines = sdssplitlen(buf, strlen(buf), "\n", 1, &nlines);
		for(i = 0; i < nlines; ++i){
			if(!(strncmp(lines[i], "#EXTINF:", strlen("#EXTINF:")) == 0))
				continue;
			char const * b = strrchr(lines[i], ','), * e;
			if(b) {
				++b;
				if(*(b) == ' ') ++b;

				e = strchr(b, ' ');
				if(!e) e = buf + strlen(buf);

				k = sdsnewlen(b, e - b);
				if(so) *so = b - buf;
				if(eo) *eo = e - buf;
			}
		}
		sdsfreesplitres(lines, nlines);
#endif /* HAVE_REGEX_H */
	return k;
}

hp_tuple2_t(sort_tvgurl_ctx, tvg_st * /*tvg*/, hp_dict_si & /*sidict*/);
static int sort_tvgurl_cb (const void * a, const void * b, void * arg)
{
	int rc;
	assert(a && b && arg);
	auto c = (sort_tvgurl_ctx *)arg;
	auto tvg = c->_1;
	auto urla = sdsdup(*(sds *)a), urlb = sdsdup(*(sds *)b);
	auto& sdsdict = c->_2;

	struct yuarel yurla, yurlb;
	rc = ((yuarel_parse(&yurla, urla) == 0 && yuarel_parse(&yurlb, urlb) == 0));
	if(!rc) rc = 1;
	else rc = (sdsdict[yurla.host] != sdsdict[yurlb.host])?
			sdsdict[yurla.host] > sdsdict[yurlb.host] :
			((sds *)a - tvg->urls < (sds *)b - tvg->urls);

	sdsfree(urla);sdsfree(urlb);
	return !rc;
}

static int sort_tvgurl(tvg_st * tvg, hp_dict_si& sidict)
{
	assert(tvg);
	sort_tvgurl_ctx c = { tvg, sidict };
	qsort_r(tvg->urls, tvg->rurls, sizeof(sds), sort_tvgurl_cb, &c);
	return 0;
}

static int hp_lfind_cb_url(const void * k, const void * e) { return !sdscmp(*(sds *)k, *(sds *)e) == 0; }

static int update_tvgurl_from_m3u(char const * m3u, tvg_st * tvg)
{
	assert(m3u && tvg);
	int i, n;
	char const * k = tvg->k, * end = m3u + strlen(m3u);
	sds pattern = sdsnew(",\\s*\("), pk = sdsempty();
	for(i = 0, n = strlen(k); i < n; ++i){
		if(k[i] != ' ' && k[i] != '-'){
			pattern = sdscatlen(pattern, k + i, 1);
			pk = sdscatlen(pk, k + i, 1);
		}
		else
			pattern = sdscatprintf(pattern, "%c?", k[i]);
	}
	pattern = sdscat(pattern, "\)\\s+");

	n = tvg->iurls;
	for(;tvg->iurls < sizeof(tvg_st::urls) / sizeof(tvg_st::urls[0]);){
		int so = 0, eo = 0;
		sds K = regex_find(m3u, pattern, &so, &eo);
		if(K){
			char const * b = strchr(m3u + eo, '\n'), * e = 0;
			if(b){
				e = strchr(++b, '\n');
				int len = (e? e : end)  - b;
				if(len > 0){
					sds url = sdsnewlen(b, len);
					if(!hp_lfind(&url, tvg->urls, tvg->iurls, sizeof(sds), hp_lfind_cb_url)) {
						tvg->urls[tvg->iurls++] = url;
					}else sdsfree(url);
				}
			}
			m3u += eo;
			sdsfree(K);
		}else break;
	}
	sdsfree(pk);
	sdsfree(pattern);

	return tvg->iurls - n;
}
/////////////////////////////////////////////////////////////////////////////////
static int on_download_m3u(hp_curl * hcurl, CURL *easy_handle, char const * url, sds str, void * arg)
{
	assert(str && arg);
	int  i = ((sds * )arg) - g_dctx->m3us;
    long res_status;
    curl_easy_getinfo(easy_handle, CURLINFO_RESPONSE_CODE, &res_status);
    g_dctx->m3us[i] = (res_status == 200? sdsdup(str) : sdsempty());

    if(res_status != 200 && hp_log_level > 0)
    	hp_log(std::cout, "%s; failed URL='%s'", __FUNCTION__, url);

	g_dctx->done = 1;
	for(i = 0; i < g_dctx->n_m3us; ++i){
		if(!g_dctx->m3us[i]) {
			g_dctx->done = 0;
			break;
		}
	}
	return 0;
}

static int on_chk_url(hp_curl * hcurl, CURL *easy_handle, char const * url, sds str, void * arg)
{
	assert(url && arg);
	int i;
	tvg_st * tvg = (tvg_st * )arg;
    long res_status;
    curl_easy_getinfo(easy_handle, CURLINFO_RESPONSE_CODE, &res_status);

	++g_chkctx->n_resp;
   //找到该频道第1个尚未被更新的URL,更新它
    if(res_status == 200){
    	for(i = 0; tvg->urls[i]; ++i){
    		if(tvg->urls[i][sdslen(tvg->urls[i]) - 1] != '\n'){
    			tvg->urls[i] = sdscatfmt(sdsempty(), "%s\n", url);
    			++tvg->rurls;
     			break;
    		}
    	}
    }
    return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////

int autoiptv_main(int argc, char **argv)
{
	int i, j, rc = 0;
#ifndef NDEBUG
//	curl_https_main();
//	rc = test_hp_curl_main(argc, argv); assert(rc == 0);
	rc = test_hp_dict_main(argc, argv); assert(rc == 0);
	if(hp_log_level == 0) hp_log_level = 2;
	{ assert(hp_isblank("")); assert(hp_isblank("\n")); assert(hp_isblank(" "));assert(hp_isblank(" \n"));}
	{ sds k = regex_find("#EXTINF:https://xxx,CCTV-1 综合\n", ",\\s*\(\\S+\)\\s+", 0, 0); assert(k && strcmp(k, "CCTV-1") == 0); sdsfree(k); }
	{ sds k = regex_find("#EXTINF:https://xxx,湖南卫视 \n", ",\\s*\(\\S+\)\\s+", 0, 0); assert(k && strcmp(k, "湖南卫视") == 0); sdsfree(k); }
	{ sds k = regex_find("#EXTINF:https://xxx,湖南卫视 \n", ",\\s*\([\u4e00-\u9fa5]+\)\\s+", 0, 0); assert(k && strcmp(k, "湖南卫视") == 0); sdsfree(k); }

	{ 	sds buf = hp_fread("cctv.m3u"); tvg_st tvgobj = { .k = sdsnew("CCTV-1") }, * tvg = &tvgobj;
		rc = update_tvgurl_from_m3u(buf, &tvgobj);assert(rc > 0);
		sdsfree(buf);sdsfree(tvg->k); }
	{ 	sds buf = hp_fread("cctv.m3u"); tvg_st tvgobj = { .k = sdsnew("nEwtV超级电影") }, * tvg = &tvgobj;
		rc = update_tvgurl_from_m3u(buf, &tvgobj);assert(rc > 0);
		sdsfree(buf);sdsfree(tvg->k); }
	{ 	sds buf = hp_fread("cctv.m3u"); tvg_st tvgobj = { .k = sdsnew("湖南卫视") }, * tvg = &tvgobj;
		rc = update_tvgurl_from_m3u(buf, &tvgobj);assert(rc > 0);
		sdsfree(buf);sdsfree(tvg->k); }
	{ 	sds buf = hp_fread("cctv.m3u"); tvg_st tvgobj = { .k = sdsnew("不存在卫视") }, * tvg = &tvgobj;
		rc = update_tvgurl_from_m3u(buf, &tvgobj);assert(rc == 0);
		sdsfree(buf);sdsfree(tvg->k); }
	//选中域名频率高的那个
	{ m3u_chk_ctx chkctxobj = {0}, * chkctx = &chkctxobj; hp_dict_si sidict;
	tvg_st tvgobj = { .urls = {sdsnew("https://node1.olelive.com:6443/live/CCTV1HD/hls.m3u8\n"),
	sdsnew("http://ottrrs.hl.chinamobile.com/PLTV/88888888/224/3221226016/index.m3u8\n")}, .rurls = 2}, * tvg = &tvgobj;;
	sidict["ottrrs.hl.chinamobile.com"] = 1;sidict["node1.olelive.com"] = 2;
	sort_tvgurl(tvg, sidict);
	assert(strcmp(tvg->urls[0], "https://node1.olelive.com:6443/live/CCTV1HD/hls.m3u8\n") == 0);
	for(i = 0; tvg->urls[i]; ++i) sdsfree(tvg->urls[i]);}
	{ m3u_chk_ctx chkctxobj = {0}, * chkctx = &chkctxobj; hp_dict_si sidict;
	 tvg_st tvgobj = { .urls = {sdsnew("http://ottrrs.hl.chinamobile.com/PLTV/88888888/224/3221226016/index.m3u8\n"),
	sdsnew("https://node1.olelive.com:6443/live/CCTV1HD/hls.m3u8\n")}, .rurls = 2}, * tvg = &tvgobj;;
	sidict["ottrrs.hl.chinamobile.com"] = 1;sidict["node1.olelive.com"] = 2;
	sort_tvgurl(tvg, sidict);
	assert(strcmp(tvg->urls[0], "https://node1.olelive.com:6443/live/CCTV1HD/hls.m3u8\n") == 0);
	for(i = 0; tvg->urls[i]; ++i) sdsfree(tvg->urls[i]);}
	//如果域名频率相同，选中最近更新的那个
	{ m3u_chk_ctx chkctxobj = {0}, * chkctx = &chkctxobj; hp_dict_si sidict;
	 tvg_st tvgobj = { .urls = {sdsnew("https://node1.olelive.com:6443/live/CCTV1HD/hls.m3u8\n"),
	sdsnew("http://ottrrs.hl.chinamobile.com/PLTV/88888888/224/3221226016/index.m3u8\n")}, .rurls = 2}, * tvg = &tvgobj;;
	sidict["ottrrs.hl.chinamobile.com"] = 2;sidict["node1.olelive.com"] = 2;
	sort_tvgurl(tvg, sidict);
	assert(strcmp(tvg->urls[0], "https://node1.olelive.com:6443/live/CCTV1HD/hls.m3u8\n") == 0);
	for(i = 0; tvg->urls[i]; ++i) sdsfree(tvg->urls[i]);}
#endif
	//--m3u, --url
	sds opt_m3ufile = sdsempty(), opt_urlfile = sdsempty();
	int opt_multi_url = 0;
	int opt_timeout = 60;
	int opt_sort_url = 0;

    /* parse argc/argv */
	int opt;
	struct option long_options[] = {
			{ "m3u\0<FILE>	    .m3u template file", required_argument, 0, 0 }
		,	{ "url\0<FILE>      text file, one URL per line", required_argument, 0, 0 }
		,	{ "timeout\0<INT>   timeout in seconds", required_argument, 0, 0 }
		,	{ "sort_url\0<0|1>  指定如何挑选频道的URL, 0:按URL的域名频率排序;1:按请求最快回应排序", required_argument, 0, 0 }
		, 	{ 0, 0, 0, 0 } };
	while (1) {
		int option_index = 0;

		opt = getopt_long(argc, argv, "mv:Vh", long_options, &option_index);
		if (opt == -1)
			break;
		char const * arg = optarg? optarg : "";
		switch (opt) {
		case 0:{
			if     (option_index == 0) opt_m3ufile = sdscpy(opt_m3ufile, arg);
			else if(option_index == 1) opt_urlfile = sdscpy(opt_urlfile, arg);
			else if(option_index == 2) { opt_timeout = atoi(arg); if(opt_timeout == 0) opt_timeout = 60; }
			else if(option_index == 3) { opt_sort_url = atoi(arg); }
			break;
		}
		case 'V':
			hp_log(std::cout, "%s-%s\n", GIT_BRANCH, GIT_COMMIT_ID);
			return 0;
		case 'm':
			opt_multi_url = 1;
			break;
		case 'v':
			hp_log_level = atoi(arg);
			break;
		case 'h':
			rc = 2;
			break;
		}
	}
	if(!(rc == 0 && sdslen(opt_m3ufile) > 0)) {
		fprintf(stdout, "autoiptv - auto update an IPTV .m3u file's URLs\n"
				"Usage: %s autoiptv --m3u cctv.m3u --url url.txt --timeout 60 --sort_url 0\nOptions:\n"
				, argv[0]);
		for(i = 0; long_options[i].name; ++i){
			fprintf(stdout, "  --%s=%s\n", long_options[i].name, strchr(long_options[i].name, '\0') + 1);
		}
		return 0;
	}
	FILE *file = fopen(opt_m3ufile, "r");
	if(!file) {
		hp_log(std::cout, "%s: unable to open file --m3u='%s'\n", __FUNCTION__, opt_m3ufile);
		return -1;
	}
	fclose(file);
	//init event loop
#if (defined HAVE_SYS_TIMERFD_H) && (defined HAVE_SYS_EPOLL_H)
	hp_epoll efdsobj, * loop = &efdsobj;
	rc = hp_epoll_init(loop, 0, 0, 0, 0); assert(rc == 0);
#else
	uv_loop_t uvloop, * loop = &uvloop; uv_loop_init(loop);
#endif

	//init curl
	hp_curl hp_curl_multiobj, * hcurl = &hp_curl_multiobj;
	rc = hp_curlinit(hcurl, loop);
	assert(rc == 0);

	//--url文件
	sds buf = hp_fread(opt_urlfile);
	if(buf){
		int n_m3uurl = 0;
		sds * m3uurls = sdssplitlen(buf, sdslen(buf), "\n", 1, &n_m3uurl);
		sdsfree(buf);

		//下载--url文件对应的.m3u文件
		g_dctx->n_m3us = hp_min(n_m3uurl, 128);
		for(i = 0; i < g_dctx->n_m3us; ++i){
			if(sdslen(m3uurls[i]) > 0 && m3uurls[i][0] != '#'){
				rc = hp_curladd(hcurl, curl_easy_init(), m3uurls[i], 0/*curl_slist * hdrs*/, 0/*form*/, ""/*char const * resp*/
						, 0/*on_progress*/, on_download_m3u, g_dctx->m3us + i/*void * arg*/, 0/*void * flags*/);
			}else g_dctx->m3us[i] = sdsempty();
		}

		time_t st =  time(0);
		do {
	#if (defined HAVE_SYS_TIMERFD_H) && (defined HAVE_SYS_EPOLL_H)
			hp_epoll_run(loop, 1);
	#else
			uv_run(loop, UV_RUN_ONCE);
	#endif
		}while(!g_dctx->done && difftime(time(0), st) < opt_timeout);

		sdsfreesplitres(m3uurls, n_m3uurl);
	}
	//读取--m3u文件
	buf = hp_fread(opt_m3ufile);
	int n_urls = 0;
	sds * urls = sdssplitlen(buf, sdslen(buf), "\n", 1, &n_urls);
	sdsfree(buf);

	//读取每一个频道,检测是否可用,如果不可用, 继续检测--urls中的URL
	for(i = 0; i < n_urls - 1 && g_chkctx->n_tvg < 20000; ++i){
		if(hp_isblank(urls[i]))	continue;
		tvg_st * tvg = g_chkctx->tvgs + g_chkctx->n_tvg;
		if(!(strncmp(urls[i], "#EXTINF:", strlen("#EXTINF:")) == 0)) {
			tvg->desc  = sdscatfmt((tvg->desc? tvg->desc : sdsempty()), "%S\n", urls[i]);
			continue;
		}
		tvg->extinf = sdscatfmt(sdsempty(), "%S\n", urls[i]);
		//tvg keyword,频道关键字
		tvg->k = regex_find(tvg->extinf, ",\\s*\([\u4e00-\u9fa5a-zA-Z0-9-]+\)\\s+", 0, 0);
		if(!tvg->k) tvg->k = sdsempty();
		//查找频道URL,该频道的所有URL都将被查检
		for(j = i + 1; j < n_urls && tvg->iurls < sizeof(tvg_st::urls) / sizeof(tvg_st::urls[0]); ++j){
			if((strncmp(urls[j], "#EXTINF:", strlen("#EXTINF:")) == 0)) {
				i = j - 1;
				break;
			}
			else if(!(hp_isblank(urls[j]) || urls[j][0] == '#' ||
					hp_lfind(&urls[j], tvg->urls, tvg->iurls, sizeof(sds), hp_lfind_cb_url))) {
				tvg->urls[tvg->iurls] = sdsdup(urls[j]);
				++tvg->iurls;
			}
		}
		//来自其它.m3u文件的URL
		if(sdslen(tvg->k) > 0){
			for(int j = 0; j < g_dctx->n_m3us; ++j){
				if(!g_dctx->m3us[j] || sdslen(g_dctx->m3us[j]) == 0) continue; //跳过空.m3u文件
				update_tvgurl_from_m3u(g_dctx->m3us[j], tvg);
			}
		}
		 //检测该频道的所有URL
		if(tvg->iurls > 0){ //至少有一个用来检测的URL
			if(hp_log_level > 0)
				hp_log(std::cout, "%s: checking tvg='%s' ...\n", __FUNCTION__, tvg->k);
			for(j = 0; j < tvg->iurls; ++j){
				if(hp_log_level > 0)
					std::cout << "\t" << tvg->urls[j] << std::endl;
				rc = hp_curladd(hcurl, curl_easy_init(), tvg->urls[j], 0/*curl_slist * hdrs*/, 0/*form*/, 0/*char const * resp*/
						, 0/*on_progress*/, on_chk_url, tvg/*void * arg*/, 0/*void * flags*/);
				if(rc == 0) ++g_chkctx->n_req;
			}
		}
		else  {
			hp_log(std::cout, "%s: NO URL for tvg='%s', skipped\n", __FUNCTION__, tvg->k);
		}

		++g_chkctx->n_tvg;
	}
	//run loop
	time_t st =  time(0);
	do{
#if (defined HAVE_SYS_TIMERFD_H) && (defined HAVE_SYS_EPOLL_H)
		hp_epoll_run(loop, 1);
#else
		uv_run(loop, UV_RUN_ONCE);
#endif
	}while(g_chkctx->n_resp < g_chkctx->n_req && difftime(time(0), st) < opt_timeout);

	hp_log(std::cout, "%s: done! updated total tvgs=%d, URL response/request=%d/%d, %d%% replied\n", __FUNCTION__
			, g_chkctx->n_tvg, g_chkctx->n_resp, g_chkctx->n_req
			, g_chkctx->n_resp == g_chkctx->n_req?100 : (int)((g_chkctx->n_resp)  / ( g_chkctx->n_req * 1.001) * 100));

	/*
	 * 频道URL挑选逻辑：
	 * 通过将URL中的域名分组得到频率,作为指标进行URL排序,将该指标最高的URL作为频道的URL,如果频率相同,则取更快响应的那个作为频道的URL
	 * 背后的想法是：如果某个域名多次出现，则可认为域名对应的服务端更稳定，从而URL的有效性更长
	 */
	hp_dict_si sidict;
	if(opt_sort_url == 0){
		//统计域名频率
		for(i = 0; i < g_chkctx->n_tvg; ++i){
			tvg_st * tvg = g_chkctx->tvgs + i;
			if(!tvg->urls[0]) continue;
			for(j = 0; j < tvg->rurls; ++j){
				sds url = sdsdup(tvg->urls[j]);
				struct yuarel yurl;
				if(yuarel_parse(&yurl, url) == 0){
					++sidict[yurl.host];
				}
				sdsfree(url);
			}
		}
		//开始排序
		for(i = 0; i < g_chkctx->n_tvg; ++i){
			tvg_st * tvg = g_chkctx->tvgs + i;
			if(tvg->rurls >= 2) sort_tvgurl(tvg, sidict);
			if(hp_log_level > 0){
				hp_log(std::cout, "%s: Chosen URL from total=%d for tvg='%s'%s\n", __FUNCTION__
						, tvg->rurls, tvg->k, (tvg->rurls > 0? ":" : ", no available URL, skipped"));
				for(j = 0; j < tvg->rurls; ++j){
					std::cout << "\t" << tvg->urls[j];
				}
			}
		}
	}
	//写回--m3u文件
	FILE * f = fopen(opt_m3ufile, "w");
	if(f){
		for(i = 0; i < g_chkctx->n_tvg; ++i){
			tvg_st * tvg = g_chkctx->tvgs + i;
			if(tvg->desc){
				fputs(tvg->desc, f);
			}
			fputs(tvg->extinf, f);
			sds murl = tvg->urls[0];  		 //主URL
			if(murl && murl[sdslen(murl) - 1] == '\n') { //有可能因超时而退出,从而没有检测完毕
				for(j = 0; tvg->urls[j] && (!opt_multi_url? j < 1 : 1); ++j){
					sds url = tvg->urls[j];
					if(url[sdslen(url) - 1] == '\n' && !hp_isblank(url))
						fputs(url, f);
					else break;
				}
			}
		}
		fclose(f);
		hp_log(std::cout, "%s: tempalte file '%s' updated\n", __FUNCTION__, opt_m3ufile);
	}
	//clean
	sdsfreesplitres(urls, n_urls);
	hp_curluninit(hcurl);
#if (defined HAVE_SYS_TIMERFD_H) && (defined HAVE_SYS_EPOLL_H)
		hp_epoll_uninit(loop);
#else
		uv_loop_close(loop);
#endif
	for(i = 0; i < g_dctx->n_m3us; ++i) {
		if(g_dctx->m3us[i]) sdsfree(g_dctx->m3us[i]);
	}
	sdsfree(opt_urlfile);
	sdsfree(opt_m3ufile);

	for(i = 0; i < g_chkctx->n_tvg; ++i){
		tvg_st * tvg = g_chkctx->tvgs + i;
		if(tvg->desc) sdsfree(tvg->desc);
		sdsfree(tvg->extinf);
		sdsfree(tvg->k);
		for(j = 0; j < tvg->iurls; ++j)
			sdsfree(tvg->urls[j]);
	}
	return 0;
}
