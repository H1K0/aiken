# Aiken

## Contents

- [About](#about)
- [How it works](#how-it-works)
- [Usage](#usage)

## About

Aiken (_jp._ 合件, _lit._ "common subject") is an utility to share files across your LAN. For example, when you are at work and need to send a file to your colleagues quickly.

## How it works

Server wants to share file, client wants to get it. Firstly, they need to find each other in the LAN. The server generates a token needed to get the file. The client transceives a broadcast packet containing this token to find the server. The server checks the token and sends back a response `OK` or `NO` depending on whether token is valid or not. If token is valid, the server sets up a TCP server for the client to send the file. When got `OK` response, the client connects to the server and gets the file.

NOTE: currently you should have only one server in your LAN at one time, because when a client sends broadcasts a token, he can receive a `NO` response from a wrong server before `OK` response from the right one, rendering himself unable to get the file.

## Usage

```
Usage:
  aiken <options> [arguments]

Options:
  -h           Print this help and exit
  -s <path>    Share the file of the specified <path>
  -g <token>   Get the file by its token
  -o <path>    Specify output path when getting file
               (defaults to current working directory and
               the initial file name)
  -V           Print version info and exit
```

---

_&copy; Masahiko AMANO aka H1K0, 2024-present_
