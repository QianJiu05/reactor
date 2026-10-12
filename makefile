# ============================================================
#  服务器端（reactor）构建脚本
#
#  epoll 多 reactor 并发服务器：
#    · 接收相机客户端的自定义协议帧
#    · 本地落盘存储
#    · 转发给查看端（HTTP MJPEG，浏览器直接打开即可观看）
#
#  用法:
#      make            # 生成 reactor
#      make clean
# ============================================================

TARGET := reactor
FAKE_CAM := fake_cam

INC_FLAG  := -Iinc -Iinc/http -I .

SRC_FILES := example/main.c \
             src/reactor.c \
             src/connect_pool.c \
             src/server_init.c \
             src/epoll.c \
             src/accept.c \
             src/recv.c \
             src/callback.c \
             src/recorder.c \
             src/account.c \
             src/stream_source.c \
             src/http/http_handler.c \
             src/http/recv_resource.c

HEADERS := config.h \
           inc/protocol.h \
           inc/recorder.h \
           inc/account.h \
           inc/account_name.h \
           inc/stream_source.h \
           inc/connect_pool.h \
           inc/reactor.h \
           inc/server_init.h \
           inc/epoll.h \
           inc/accept.h \
           inc/recv.h \
           inc/callback.h \
           inc/http/http_handler.h \
           inc/http/http_parse.h \
           inc/http/recv_resource.h \
           inc/http/http_helper.h \
           inc/http/struct_http.h

CFLAGS := -O2 -Wall -Wextra $(INC_FLAG) -D_GNU_SOURCE

.PHONY: all clean placeholder-image

all: $(TARGET) $(FAKE_CAM)

$(TARGET): $(SRC_FILES) $(HEADERS)
	gcc $(CFLAGS) -o $@ $(SRC_FILES) -lpthread

# 没有开发板时的假相机：把一个 JPEG 按同样协议反复推给服务器，
# 用来验证「落盘 + 转发」链路。
$(FAKE_CAM): test/fake_cam.c inc/protocol.h
	gcc $(CFLAGS) -o $@ test/fake_cam.c

# waiting.jpg 是「查看端先连、采集端后连」时推给浏览器的占位图。
# 仓库里放的是 test/pic2.jpg 的副本，仅用于验证链路 ——
# 建议自己画一张写着「等待设备上线…」的 640x480 图覆盖它。
# 也可以把 config.h 的 USING_WAITING_IMG 置 0 直接关掉该功能。
placeholder-image:
	@echo "占位图: waiting.jpg"
	@if [ -f waiting.jpg ]; then \
		echo "  状态: 已存在 ($$(wc -c < waiting.jpg) 字节)"; \
	else \
		echo "  状态: 缺失 —— 查看端先连时将保持空白（功能自动降级）"; \
	fi
	@echo "  替换: 用任意画图工具导出一张 JPEG 覆盖 waiting.jpg 即可"

clean:
	rm -f $(TARGET) $(FAKE_CAM)
	rm -rf records
