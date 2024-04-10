/*!
 * 2024/4/10
 * 以样本m3u文件--m3u为模板，使用--urls中的URL，通过关键字匹配，检测并更新其中的URL
 * autoiptv --m3u autoiptv.m3u --urls urls.txt
 *
 * 更新前autoiptv.m3u:
 *
 * #EXTM3U x-tvg-url="https://...
 * #EXTINF:-1 tvg-id="A01" tvg-name="CCTV1" tvg-logo="https://cdn.jsdelivr.net/gh/wanglindl/TVlogo@main/img/CCTV1.png" group-title="央视",CCTV-1 综合
 * http://[2409:8087:7000:20:1000::22]:6060/yinhe/2/ch00000090990000001331/index.m3u8?virtualDomain=yinhe.live_hls.zte.com
 * #EXTINF:-1 tvg-id="A02" tvg-name="CCTV2" tvg-logo="https://cdn.jsdelivr.net/gh/wanglindl/TVlogo@main/img/CCTV2.png" group-title="央视",CCTV-2 财经
 * http://[2409:8087:7000:20:1000::22]:6060/yinhe/2/ch00000090990000001332/index.m3u8?virtualDomain=yinhe.live_hls.zte.com
 *
 * urls.txt:
 *
 * https://gitee.com/happy-is-not-closed/IPTV/raw/main/IPTV.m3u
 * https://iptv-org.github.io/iptv/countries/cn.m3u
 *
 * 更新后autoiptv.m3u
 *
 * #EXTM3U x-tvg-url="https://...
 * #EXTINF:-1 tvg-id="A01" tvg-name="CCTV1" tvg-logo="https://cdn.jsdelivr.net/gh/wanglindl/TVlogo@main/img/CCTV1.png" group-title="央视",CCTV-1 综合
 * http://39.134.24.162/dbiptv.sn.chinamobile.com/PLTV/88888890/224/3221225804/index.m3u8
 * #EXTINF:-1 tvg-id="A02" tvg-name="CCTV2" tvg-logo="https://cdn.jsdelivr.net/gh/wanglindl/TVlogo@main/img/CCTV2.png" group-title="央视",CCTV-2 财经
 * http://39.134.24.162/dbiptv.sn.chinamobile.com/PLTV/88888890/224/3221226195/index.m3u8
 *
 */
#include <stdio.h>

int main(int argc, char **argv)
{
	//读取--m3u文件
	//遍历每一个tvg/频道
		//检测是否可用,如果不可用, 继续检测--urls中的URL
	//写回--m3u文件
	return 0;
}
