import socket
import struct
import sys
from Crypto.Cipher import DES

def vnc_encrypt(password, challenge):
    key = password.ljust(8, '\0').encode('ascii')
    rev_key = bytearray(8)
    for i in range(8):
        k = key[i]
        rev_key[i] = int(f'{k:08b}'[::-1], 2)
    cipher = DES.new(rev_key, DES.MODE_ECB)
    return cipher.encrypt(challenge)

def start_server():
    host = '0.0.0.0'
    port = 5901
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind((host, port))
    s.listen(1)
    print(f"Mock VNC Server listening on {host}:{port}")
    print("Waiting for BKOS to connect...")
    
    conn, addr = s.accept()
    print(f"Connected by {addr}")
    
    # 1. Отправляем версию
    conn.sendall(b"RFB 003.008\n")
    client_ver = conn.recv(12)
    print(f"Client version: {client_ver.strip().decode()}")
    
    # 2. Отправляем только тип 2 (VNC Auth)
    conn.sendall(bytes([1, 2]))
    
    selected_type = conn.recv(1)
    print(f"Selected type: {selected_type.hex()}")
    
    if selected_type != b'\x02':
        print("Client did not select VNC Auth!")
        conn.close()
        return

    # 3. Отправляем 16-байтовый Challenge
    challenge = bytes.fromhex("0102030405060708090A0B0C0D0E0F10")
    conn.sendall(challenge)
    print(f"Sent Challenge: {challenge.hex().upper()}")
    
    # 4. Получаем Response от BKOS
    response = conn.recv(16)
    print(f"Received Response: {response.hex().upper()}")
    
    # 5. Считаем правильный ответ
    expected_1234 = vnc_encrypt("1234", challenge)
    expected_9178 = vnc_encrypt("9178", challenge)
    
    print(f"Expected for 1234: {expected_1234.hex().upper()}")
    print(f"Expected for 9178: {expected_9178.hex().upper()}")
    
    if response == expected_1234:
        print("\n=== AUTH OK for 1234 ===")
    elif response == expected_9178:
        print("\n=== AUTH OK for 9178 ===")
    else:
        print("\n=== AUTH FAILED (DES BUG in BKOS) ===")

    # 6. Отправляем OK и закрываем
    conn.sendall(struct.pack(">I", 0))
    conn.close()
    s.close()

if __name__ == "__main__":
    start_server()
