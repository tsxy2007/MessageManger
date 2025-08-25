import socket
import threading
import struct
from datetime import datetime
from collections import defaultdict
import json

# 定义头部结构体格式 (匹配FChunkHeader)
# 4字节MessageId(uint32) + 4字节TotalLength(uint32) + 4字节ChunkIndex(uint32) + 1字节IsLastChunk(uint8)
# 使用小端字节序('<')匹配多数系统
HEADER_FORMAT = '<IIII'
HEADER_SIZE = struct.calcsize(HEADER_FORMAT)  # 13字节

class FragmentedMessageServer:
    def __init__(self, host='0.0.0.0', port=12345):
        self.host = host
        self.port = port
        self.server_socket = None
        self.is_running = False
        self.clients = []
        
        # 用于缓存分片消息
        self.fragment_cache = defaultdict(dict)
        # 记录每个消息的总长度
        self.message_total_lengths = {}
        # 消息ID计数器，用于服务器发送消息时生成唯一ID
        self.next_message_id = 1

    def start(self):
        """启动服务器"""
        try:
            self.server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            self.server_socket.bind((self.host, self.port))
            self.server_socket.listen(5)
            self.is_running = True
            
            print(f"分片消息服务器已启动，监听 {self.host}:{self.port}...")
            print(f"启动时间: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
            print(f"头部格式: {HEADER_SIZE}字节 ({HEADER_FORMAT})，匹配FChunkHeader结构体")
            print("支持收发分片消息并自动合并")
            
            # 启动接收连接线程
            accept_thread = threading.Thread(target=self.accept_connections, daemon=True)
            accept_thread.start()
            
            # 移除自动发送消息线程，改为被动回复
            
            while self.is_running:
                cmd = input("输入 'exit' 关闭服务器: ")
                if cmd.lower() == 'exit':
                    self.stop()
                    
        except Exception as e:
            print(f"启动服务器失败: {e}")
            self.stop()

    def accept_connections(self):
        """接受客户端连接"""
        while self.is_running:
            try:
                client_socket, client_address = self.server_socket.accept()
                print(f"\n新连接: {client_address}")
                print(f"连接时间: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
                
                self.clients.append(client_socket)
                
                client_thread = threading.Thread(
                    target=self.handle_client,
                    args=(client_socket, client_address),
                    daemon=True
                )
                client_thread.start()
                
            except Exception as e:
                if self.is_running:
                    print(f"接受连接时出错: {e}")

    def handle_client(self, client_socket, client_address):
        """处理客户端发送的分片消息"""
        try:
            
            while self.is_running:
                # 1. 接收消息头部
                header_data = b''
                while len(header_data) < HEADER_SIZE:
                    chunk = client_socket.recv(HEADER_SIZE - len(header_data))
                    if not chunk:
                        print(f"\n客户端 {client_address} 断开连接")
                        return
                    header_data += chunk
                
                # 解析头部
                try:
                    message_id, total_length, chunk_index, is_last_chunk = struct.unpack(
                        HEADER_FORMAT, header_data)
                    
                    # 检查退出命令
                    if message_id == 0:
                        response = "再见！\n"
                        self.send_fragmented_message(client_socket, response.encode('utf-8'))
                        print(f"客户端 {client_address} 请求断开连接")
                        break
                        
                    print(f"\n[{datetime.now().strftime('%H:%M:%S')}] "
                          f"收到分片 - MessageId: {message_id}, "
                          f"总长度: {total_length}, "
                          f"分片索引: {chunk_index}, "
                          f"是否最后分片: {'是' if is_last_chunk else '否'}")
                    
                except struct.error as e:
                    print(f"解析头部失败: {e}")
                    continue
                
                # 2. 接收消息体
                if is_last_chunk:
                    # 最后一个分片：计算剩余长度
                    received_length = sum(len(data) for data in self.fragment_cache[message_id].values())
                    body_length = total_length - received_length
                else:
                    # 非最后一个分片：使用默认缓冲区大小
                    body_length = 1024  # 可根据实际情况调整
                
                # 接收消息体
                body_data = b''
                while len(body_data) < body_length:
                    chunk = client_socket.recv(min(body_length - len(body_data), 1024))
                    if not chunk:
                        print(f"\n客户端 {client_address} 意外断开连接")
                        return
                    body_data += chunk
                
                print(f"接收消息体: {len(body_data)} 字节")
                
                # 3. 缓存当前分片
                self.fragment_cache[message_id][chunk_index] = body_data
                self.message_total_lengths[message_id] = total_length
                
                # 4. 检查是否是最后一个分片，如果是则尝试合并消息
                if is_last_chunk:
                    self.try_assemble_message(message_id, client_address, client_socket)
                
        except Exception as e:
            print(f"处理客户端 {client_address} 时出错: {e}")
        finally:
            client_socket.close()
            if client_socket in self.clients:
                self.clients.remove(client_socket)

    def try_assemble_message(self, message_id, client_address, client_socket):
        """尝试合并所有分片为完整消息，并回复确认"""
        try:
            chunks = self.fragment_cache.get(message_id, {})
            if not chunks:
                print(f"消息 {message_id} 没有任何分片数据")
                return 
                
            total_length = self.message_total_lengths.get(message_id, 0)
            
            # 检查是否所有分片都已收到
            max_index = max(chunks.keys())
            all_received = True
            for i in range(max_index + 1):
                if i not in chunks:
                    print(f"消息 {message_id} 缺少分片 {i}")
                    all_received = False
                    break
            
            if not all_received:
                print(f"消息 {message_id} 分片不完整，无法合并")
                return 
                
            # 按索引排序并合并分片
            sorted_chunks = [chunks[i] for i in sorted(chunks.keys())]
            full_message = b''.join(sorted_chunks)
            
            # 验证总长度
            if len(full_message) != total_length:
                print(f"消息 {message_id} 长度不匹配: "
                      f"预期 {total_length} 字节，实际 {len(full_message)} 字节")
                return 
                
            print(f"\n===== 消息 {message_id} 合并完成 =====")
            print(f"总长度: {len(full_message)} 字节")
            print(f"分片数量: {len(chunks)} 个")
            
            # 尝试解析为字符串
            try:
                message_str = full_message.decode('utf-8')
                print(f"解析为字符串: {message_str}")
                
                # 收到消息后自动回复（核心修改点：被动回复逻辑）
                response = json.dumps({
                    "Type": "Heartbeat",
                    "Data": "12312312",
                    "Timestamp": datetime.now().strftime('%Y-%m-%d %H:%M:%S')
                }, ensure_ascii=False, separators=(',', ':'))
                self.send_fragmented_message(client_socket, response.encode('utf-8'))
                
            except UnicodeDecodeError:
                print("无法解析为UTF-8字符串（可能是二进制数据）")
                # 发送二进制消息确认
                response = json.dumps({
                    "Type": "BinaryResponse",
                    "Data": f"已收到二进制消息，长度: {len(full_message)}字节"
                }, ensure_ascii=False, separators=(',', ':'))
                self.send_fragmented_message(client_socket, response.encode('utf-8'))
            
            print("====================================\n")
            
            # 清除缓存
            del self.fragment_cache[message_id]
            del self.message_total_lengths[message_id]
            
        except Exception as e:
            print(f"合并消息 {message_id} 失败: {e}")

    def send_fragmented_message(self, client_socket, data):
        """按照FChunkHeader格式分块发送消息"""
        if not data:
            return
        
        print(f"发送的消息是：{data}")
        message_id = self.next_message_id
        self.next_message_id += 1  # 递增消息ID
        total_length = len(data)
        chunk_size = 1024  # 每个分片的大小
        num_chunks = (total_length + chunk_size - 1) // chunk_size  # 计算总分片数
        
        print(f"\n开始分块发送消息 - MessageId: {message_id}, 总长度: {total_length}, 分片数: {num_chunks}")
        
        for chunk_index in range(num_chunks):
            # 计算当前分片的起始和结束位置
            start = chunk_index * chunk_size
            end = min(start + chunk_size, total_length)
            chunk_data = data[start:end]
            
            # 判断是否为最后一个分片
            is_last_chunk = 1 if (chunk_index == num_chunks - 1) else 0
            
            # 打包头部
            header = struct.pack(
                HEADER_FORMAT,
                message_id,
                total_length,
                chunk_index,
                is_last_chunk
            )
            
            # 发送头部+数据
            try:
                client_socket.sendall(header + chunk_data)
                print(f"发送分片 - Index: {chunk_index}, 大小: {len(chunk_data)} 字节, 最后分片: {'是' if is_last_chunk else '否'}")
            except Exception as e:
                print(f"发送分片 {chunk_index} 失败: {e}")
                return

    def stop(self):
        """停止服务器"""
        self.is_running = False
        print("\n正在关闭服务器...")
        
        for client_socket in self.clients:
            try:
                response = f"服务器正在关闭，连接将断开。"
                self.send_fragmented_message(client_socket, response.encode('utf-8'))
                client_socket.close()
            except Exception as e:
                print(f"关闭客户端连接时出错: {e}")
        
        if self.server_socket:
            try:
                self.server_socket.close()
            except Exception as e:
                print(f"关闭服务器socket时出错: {e}")
        
        print("服务器已关闭")

if __name__ == "__main__":
    server = FragmentedMessageServer(host='127.0.0.1', port=12345)
    server.start()