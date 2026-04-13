```
  🔱  T R I T O N T A L K  —  S L I D I N G   W I N D O W   T C P  🔱

     SENDER (HOST A)                          RECEIVER (HOST B)
    .-----------.                              .-----------.
    |  .-----. |    [SEQ 0]─────────────────> |  .-----. |
    |  |     | |    [SEQ 1]─────────────────> |  |     | |
    |  | >_  | |    [SEQ 2]─────────────────> |  | >_  | |
    |  |     | |    [SEQ 3]─────────────────> |  |     | |
    |  '-----' | <─────────────────[ACK 4]    |  '-----' |
    |    🔱    |                              |    🔱    |
    '-----------'   window: [0][1][2][3]      '-----------'
         |||          ^^ 4 in-flight ^^            |||
        _|||_                                     _|||_
       [_____]                                   [_____]
        HOST A                                    HOST B
```

# TritonTalk

A simulated Ethernet switch with reliable transport — hosts send and receive
framed messages through a virtual switch with configurable corruption, window
sizing, and congestion control.

---

## Project Info

| Field | Value |
|-------|-------|
| **Name** | **Aidan Murphy**|
| **PID**  | **A18483865**|

---

## Running

> Requires Docker installed on your machine. Builds a clean Ubuntu 22.04 container and mounts the current
> directory — no local dependencies needed.

If you prefer to develop entirely in the terminal (with VIM no IDE required), you can use
`./dockerrun.sh` to run any command inside the Docker container. For example:

```bash
./dockerrun.sh make                                  # compile the project
./dockerrun.sh ./tritontalk test_suite/basic.cfg     # run tritontalk directly
./dockerrun.sh ./tester.sh                           # run the test suite
```

Alternatively, you can develop and run the project inside **VS Code** using the
Dev Container setup described in the project description.

---

## Testing

Test cases live in `test_suite/test{1,2,3}/`. Each test compares `stdout.txt`
against `exp_out.txt`. CSV diagnostics are written to `csv_files/`.

---

## Config Files

| File                      | Description                 |
|---------------------------|-----------------------------|
| `test_suite/basic.cfg`    | Basic 2-host setup          |
| `test_suite/cc_basic.cfg` | Congestion control baseline |
| `test_suite/sw4.cfg`      | 4-port switch topology      |
