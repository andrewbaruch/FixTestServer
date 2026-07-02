import socket
import datetime
import time

class SimpleFIXClient:
    def __init__(self, host, port, sender, target):
        self.host = host
        self.port = port
        self.sender = sender
        self.target = target
        self.seq_num = 1
        self.sock = None

    def connect(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.connect((self.host, self.port))
        print(f"Connected to {self.host}:{self.port}")

    def disconnect(self):
        if self.sock:
            self.sock.close()

    def make_header(self, msg_type):
        now = datetime.datetime.utcnow().strftime('%Y%m%d-%H:%M:%S.000')
        header = f"35={msg_type}\x0149={self.sender}\x0156={self.target}\x0134={self.seq_num}\x0152={now}\x01"
        self.seq_num += 1
        return header

    def send_msg(self, msg_type, body_fields):
        body = "".join(f"{k}={v}\x01" for k, v in body_fields.items())
        header = self.make_header(msg_type)
        content = header + body
        
        # Calculate length (from 35= to end of body)
        body_length = len(content)
        
        # Start constructing the message
        msg_without_checksum = f"8=FIX.4.2\x019={body_length}\x01{content}"
        
        # Calculate checksum
        checksum = sum(ord(c) for c in msg_without_checksum) % 256
        checksum_str = f"{checksum:03d}"
        
        full_msg = f"{msg_without_checksum}10={checksum_str}\x01"
        print(f"Sending: {full_msg.replace(chr(1), '|')} ")
        self.sock.sendall(full_msg.encode('ascii'))

    def logon(self):
        self.send_msg("A", {"98": "0", "108": "30"})

    def logout(self):
        self.send_msg("5", {})

    def new_order_single(self, clordid, symbol, side, qty, price=None, account="TEST_ACC"):
        fields = {
            "1": account,
            "11": clordid,
            "55": symbol,
            "54": side,
            "40": "2" if price else "1",
            "38": qty,
            "21": "1" # HandlInst (1 = automated execution private)
        }
        if price:
            fields["44"] = price
        self.send_msg("D", fields)

    def cancel_replace_order(self, orig_clordid, clordid, qty, account="TEST_ACC", side="1"):
        fields = {
            "1": account,
            "11": clordid,
            "41": orig_clordid,
            "38": qty,
            "54": side,
            "21": "1"
        }
        self.send_msg("G", fields)

    def cancel_order(self, orig_clordid, clordid, order_id=None):
        fields = {
            "11": clordid,
            "41": orig_clordid
        }
        if order_id:
            fields["37"] = order_id
        self.send_msg("F", fields)

    def order_status_request(self, clordid, symbol=None, side="1"):
        fields = {
            "11": clordid,
            "54": side
        }
        if symbol:
            fields["55"] = symbol
        self.send_msg("H", fields)

    def mass_status(self, req_id, req_type, symbol=None):
        fields = {
            "584": req_id,
            "585": req_type
        }
        if symbol:
            fields["55"] = symbol
        self.send_msg("UAF", fields)

    def receive(self, timeout=2.0):
        self.sock.settimeout(timeout)
        try:
            while True:
                data = self.sock.recv(4096)
                if not data:
                    break
                messages = data.decode('ascii').split("10=")
                for msg in messages:
                    if msg and "8=FIX" in msg:
                        print(f"Received: {msg.replace(chr(1), '|')}10=...")
                if len(data) < 4096:
                    break
        except socket.timeout:
            print("Timeout waiting for data.")
        except Exception as e:
            print(f"Error receiving: {e}")

if __name__ == "__main__":
    client = SimpleFIXClient("127.0.0.1", 5001, "CLIENT", "CQG")
    client.connect()
    
    print("\n--- Sending Logon ---")
    client.logon()
    client.receive(1.0)
    
    print("\n--- Sending NewOrderSingle (Missing Account - Should Reject) ---")
    client.send_msg("D", {
        "11": "REJ_001", "55": "AAPL", "54": "1", "40": "1", "38": "100", "21": "1"
    })
    client.receive(1.0)
    
    print("\n--- Sending NewOrderSingle (Small) ---")
    client.new_order_single("ORD_001", "F.US.CLF27", "1", "50")
    client.receive(1.0)
    
    print("\n--- Sending OrderCancelReplaceRequest (Bad Side - Should Reject) ---")
    client.cancel_replace_order("ORD_001", "REP_001", "100", side="2")
    client.receive(1.0)

    print("\n--- Sending OrderCancelRequest (Unknown Order - Should Reject) ---")
    client.cancel_order("NONEXISTENT", "CXL_001")
    client.receive(1.0)

    print("\n--- Sending NewOrderSingle (Large - for cancel/replace) ---")
    client.new_order_single("ORD_002", "AAPL", "1", "500")
    client.receive(0.5)

    print("\n--- Sending OrderStatusRequest ---")
    client.order_status_request("ORD_002", "AAPL", "1")
    client.receive(0.5)

    print("\n--- Sending OrderCancelReplaceRequest (Success - reduce qty to 200) ---")
    client.cancel_replace_order("ORD_002", "REP_002", "200", side="1")
    client.receive(1.0)
    
    print("\n--- Sending OrderCancelRequest (Success) ---")
    # Using REP_002 as the new origClOrdId since the replace was accepted
    client.cancel_order("REP_002", "CXL_002")
    client.receive(1.0)
    
    print("\n--- Sending OrderMassStatusRequest ---")
    client.mass_status("MASS_001", "7")
    client.receive(1.0)
    
    print("\n--- Testing F.US.NQU26 ---")
    client.new_order_single("ORD_NQU26", "F.US.NQU26", "1", "1")
    client.receive(1.0)

    print("\n--- Testing F.US.CNU26 ---")
    client.new_order_single("ORD_CNU26", "F.US.CNU26", "1", "1")
    client.receive(1.0)
    
    print("\n--- Sending Logout ---")
    client.logout()
    client.receive(1.0)
    
    client.disconnect()
