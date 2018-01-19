/* vim: set ai et ts=4 sw=4: */
package main

import (
    "os"
    "log"
    "net"
    "time"
    "strconv"
    "strings"
)

func sendMessage(ip string, port int) {
    addr := strings.Join([]string{ip, strconv.Itoa(port)}, ":")
    conn, err := net.Dial("tcp", addr)

    if conn == nil {
        log.Fatalf("Connection failed!")
    }

    if err != nil {
        log.Fatalln(err)
    }

    defer conn.Close()

    // send one message in two packages
    conn.Write([]byte("Hello!"))
    conn.Write([]byte("\r\n"))
}

func receiverProc(num int, total int, readych chan int, donech chan int, ip string, port int) {
    addr := strings.Join([]string{ip, strconv.Itoa(port)}, ":")
    conn, err := net.Dial("tcp", addr)

    if conn == nil {
        log.Fatalf("Connection failed!")
    }

    if err != nil {
        log.Fatalln(err)
    }

    defer conn.Close()

    donech <- 0x0C

    // wait until all client connect
    <-readych
    log.Printf("[%d] ready!", num)

    buff := make([]byte, 64)

    n := 0
    for {
        n, _ = conn.Read(buff)
        if n > 0 {
            break
        }
    }
    log.Printf("[%d] receive: %d bytes", num, n)

    donech <- 0x0D
}

func main() {
    if len(os.Args) < 4 {
        log.Printf("Usage: %s ip port conn_number", os.Args[0])
        return
    }

    ip := os.Args[1]
    port, _ := strconv.Atoi(os.Args[2])
    total, _ := strconv.Atoi(os.Args[3])

    donech := make(chan int, total)
    readych := make(chan int, total)

    for i := 0; i < total; i++ {
        log.Printf("[main]: creating client, i = %d", i)
        go receiverProc(i, total, readych, donech, ip, port)
        time.Sleep(1 * time.Millisecond);
    }

    // to make sure every client is connected
    log.Printf("[main]: making sure all clients are connected")
    for i := 0; i < total; i++ {
        <-donech
        log.Printf("[main]: connected, i = %d", i)
    }

    log.Printf("[main]: starting test")
    sendMessage(ip, port)
    log.Printf("[main]: message sent")

    log.Printf("[main]: writing to the readych...")
    for i := 0; i < total; i++ {
        readych <- 0x01
    }

    for i := 0; i < total; i++ {
        <-donech
        log.Printf("[main]: done, i = %d", i)
    }

    log.Printf("Test passed, %d connections handled!", total)
}
