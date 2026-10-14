Part of the shared knowledge; index and source conventions in `docs/agents/KNOWLEDGE.md`.

## Network: transport (Tudp)

- **Datagram header:** `[seq u16]`. An odd seq means unreliable; it may start with ACK/control triples
  (first byte 1..4, 3 bytes each). An even, non-zero seq means reliable, and the receiver ACKs it with
  `[own hdr][0x01][seq u16]`.
- **Messages:** several messages are concatenated in one payload. Every message length below is
  verified, because an unknown opcode makes the parser drop the rest of the datagram.
