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
	sds urls[5];
};

typedef struct m3u_download_ctx {
	sds m3us[128];	//每个元素包含一个m3u文件的全部内容
	int n_m3us;
	int done;
} m3u_download_ctx;

typedef struct m3u_chk_ctx {
	tvg_st tvgs[20000];
	int n_tvg, n_chked;
} m3u_chk_ctx;
/////////////////////////////////////////////////////////////////////////////////////////
static 	m3u_download_ctx dctxobj = { 0 }, * g_dctx = &dctxobj;
static 	m3u_chk_ctx g_chkctxobj = {0}, * g_chkctx = &g_chkctxobj;

/////////////////////////////////////////////////////////////////////////////////////////
static sds regex_find(char const *buf, char const * pattern, int * so, int * eo){
	assert(buf && pattern);
	int rc;
	sds k = 0;
#ifdef HAVE_REGEX_H
		regex_t start_state;
		regmatch_t matchptr[2] = { 0 };
		rc = regcomp(&start_state, pattern, REG_EXTENDED);
		if(!(rc == 0)) return sdsempty();

		int status = regexec(&start_state, buf, sizeof(matchptr) / sizeof(matchptr[0]), matchptr, 0);
		regfree(&start_state);
		if(status == 0){
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
	return k? k : sdsempty();
}

static sds find_url_in_m3u_file(char const * m3u, char const * k){
	assert(m3u && k);
	sds url = 0;
	sds pattern = sdscatfmt(sdsempty(), ",\\s*\(%s\)", k);
	int so = 0, eo = 0;
	sds K = regex_find(m3u, pattern, &so, &eo);
	if(strcmp(K, k) == 0){
		char const * b = strchr(m3u + eo, '\n'), * e = strchr((b? ++b : ""), '\n');
		if(b) url  = sdsnewlen(b, (e? e : (m3u + strlen(m3u)))  - b);
	}
	sdsfree(K);
	sdsfree(pattern);
	return url? url : sdsempty();
}
/////////////////////////////////////////////////////////////////////////////////
static int on_download_m3u(hp_curl * hcurl, CURL *easy_handle, char const * url, sds str, void * arg)
{
	assert(str && arg);
	int  i = ((sds * )arg) - g_dctx->m3us;
    long res_status;
    curl_easy_getinfo(easy_handle, CURLINFO_RESPONSE_CODE, &res_status);
    g_dctx->m3us[i] = (res_status == 200? sdsdup(str) : sdsempty());

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

    //找到该频道第1个尚未被更新的URL,更新它
    if(res_status == 200){
    	for(i = 0; tvg->urls[i]; ++i){
    		if(tvg->urls[i][sdslen(tvg->urls[i]) - 1] != '\n'){
    			tvg->urls[i] = sdscatfmt(sdsempty(), "%s\n", url);
    			if(i == 0){
					++g_chkctx->n_chked;
					if(hp_log_level > 0){
						hp_log(std::cout, "%s: [%d/%d] URL added for tvg='%s', url='%s'\n", __FUNCTION__
								, g_chkctx->n_chked, g_chkctx->n_tvg, tvg->k, url);
					}
    			}
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
	if(hp_log_level == 0) hp_log_level = 2;
	{ assert(hp_isblank("")); assert(hp_isblank("\n")); assert(hp_isblank(" "));assert(hp_isblank(" \n"));}
	{ sds k = regex_find("#EXTINF:https://xxx,CCTV-1 综合\n", ",\\s*\(\\S+\)\\s+", 0, 0); assert(strcmp(k, "CCTV-1") == 0); sdsfree(k); }
	{ sds k = regex_find("#EXTINF:https://xxx,湖南卫视 \n", ",\\s*\(\\S+\)\\s+", 0, 0); assert(strcmp(k, "湖南卫视") == 0); sdsfree(k); }
	{ sds k = regex_find("#EXTINF:https://xxx,湖南卫视 \n", ",\\s*\([\u4e00-\u9fa5]+\)\\s+", 0, 0); assert(strcmp(k, u8"湖南卫视") == 0); sdsfree(k); }

//	{ sds buf = hp_fread("cctv.m3u"), url = find_url_in_m3u_file(buf, "湖南卫视");assert(sdslen(url) > 0); sdsfree(buf); }
	{ sds buf = hp_fread("cctv.m3u"),url = find_url_in_m3u_file(buf, "不存在卫视"); assert(sdslen(url) == 0); sdsfree(buf); }

#endif
	//--m3u, --url
	sds opt_m3ufile = sdsempty(), opt_urlfile = sdsempty();
	int opt_multi_url = 0;
    /* parse argc/argv */
	int opt;
	while (1) {
		int option_index = 0;
		static struct option long_options[] = {
				{ "m3u", required_argument, 0, 0 }
			,	{ "url", required_argument, 0, 0 }
			, 	{ 0, 0, 0, 0 } };

		opt = getopt_long(argc, argv, "mv:Vh", long_options, &option_index);
		if (opt == -1)
			break;
		char const * arg = optarg? optarg : "";
		switch (opt) {
		case 0:{
			if     (option_index == 0) opt_m3ufile = sdscpy(opt_m3ufile, arg);
			else if(option_index == 1) opt_urlfile = sdscpy(opt_urlfile, arg);
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
		hp_log(std::cout, "autoiptv - auto update an IPTV .m3u file's URLs\n"
				"Usage: %s autoiptv --m3u cctv.m3u --url url.txt\n"
				, argv[0]);
		return 0;
	}

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
	for(;!g_dctx->done;)
#if (defined HAVE_SYS_TIMERFD_H) && (defined HAVE_SYS_EPOLL_H)
		hp_epoll_run(loop, 1);
#else
		uv_run(loop, UV_RUN_ONCE);
#endif

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
		tvg->k = regex_find(tvg->extinf, ",\\s*\([\u4e00-\u9fa5a-zA-Z0-9-]+\)", 0, 0);
		//查找频道URL,该频道的所有URL都将被查检
		int iurls = 0;
		for(j = i + 1; j < n_urls && iurls < 5; ++j){
			if((strncmp(urls[j], "#EXTINF:", strlen("#EXTINF:")) == 0)) {
				i = j - 1;
				break;
			}
			else if(!(hp_isblank(urls[j]) || urls[j][0] == '#')) {
				tvg->urls[iurls] = sdsdup(urls[j]);
				++iurls;
			}
		}
		//来自其它.m3u文件的URL
		if(sdslen(tvg->k) > 0){
			for(int j = 0; j < g_dctx->n_m3us && iurls < 5; ++j){
				if(sdslen(g_dctx->m3us[j]) == 0) continue; //跳过空.m3u文件
				sds url = find_url_in_m3u_file(g_dctx->m3us[j], tvg->k);
				if(sdslen(url) > 0) {
					tvg->urls[iurls] = url;
					++iurls;
				}
				else sdsfree(url);
			}
		}
		 //检测该频道的所有URL
		if(hp_log_level > 0)
			hp_log(std::cout, "%s: checking tvg='%s' ...\n", __FUNCTION__, tvg->k);
		for(j = 0; j < iurls; ++j){
			if(hp_log_level > 0)
				std::cout << "\t" << tvg->urls[j] << std::endl;
			rc = hp_curladd(hcurl, curl_easy_init(), tvg->urls[j], 0/*curl_slist * hdrs*/, 0/*form*/, 0/*char const * resp*/
					, 0/*on_progress*/, on_chk_url, tvg/*void * arg*/, 0/*void * flags*/);
		}
		++g_chkctx->n_tvg;
	}
	//run loop
	int const timeout =
#ifndef NDEBUG
			30;
#else
			300;
#endif
	time_t st =  time(0);
	int n_chked;
	do{
#if (defined HAVE_SYS_TIMERFD_H) && (defined HAVE_SYS_EPOLL_H)
		hp_epoll_run(loop, 1);
#else
		uv_run(loop, UV_RUN_ONCE);
#endif
		//查检下是否所有URL都已检测更新完毕
		n_chked = 0;
		for(i = 0; i < g_chkctx->n_tvg; ++i){
			sds murl = g_chkctx->tvgs[i].urls[0];
			if(murl && murl[sdslen(murl) - 1] == '\n') { //有的频道可能并无URL(或已注释)
				++n_chked;
			}
		}
	}while(n_chked != g_chkctx->n_tvg && difftime(time(0), st) < timeout);
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
					if(url[sdslen(url) - 1] == '\n')
						fputs(url, f);
					else break;
				}
			}
		}
		fclose(f);
	}
	//clean
	sdsfreesplitres(urls, n_urls);
	sdsfreesplitres(m3uurls, n_m3uurl);
	hp_curluninit(hcurl);
#if (defined HAVE_SYS_TIMERFD_H) && (defined HAVE_SYS_EPOLL_H)
		hp_epoll_uninit(loop);
#else
		uv_loop_close(loop);
#endif
	for(i = 0; i < g_dctx->n_m3us; ++i) sdsfree(g_dctx->m3us[i]);
	sdsfree(opt_urlfile);
	sdsfree(opt_m3ufile);

	for(i = 0; i < g_chkctx->n_tvg; ++i){
		tvg_st * tvg = g_chkctx->tvgs + i;
		if(tvg->desc) sdsfree(tvg->desc);
		sdsfree(tvg->extinf);
		sdsfree(tvg->k);
		for(j = 0; tvg->urls[j]; ++j)
			sdsfree(tvg->urls[j]);
	}
	return 0;
}
