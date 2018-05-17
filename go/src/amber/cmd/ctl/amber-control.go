// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"amber/ipcserver"
	"app/context"
	"flag"
	"fmt"
	"fidl/amber"
	"os"
	"strings"
	"time"

	"syscall/zx"
	"syscall/zx/zxwait"
)

const usage = `usage: amber_ctl <command> [opts]
Commands
    get_up    - get an update for a package
      Options
        -n:      name of the package
        -v:      version of the package to retrieve, if none is supplied any
                 package instance could match
        -m:      merkle root of the package to retrieve, if none is supplied
                 any package instance could match
        -nowait: exit once package installation has started, but don't wait for
                 package activation

    get_blob  - get the specified content blob
        -i: content ID of the blob

    add_src   - add a source to the list we can use
        -s: location of the package source
        -k: root key for the source, either a file or http[s] URL or the key
            itself
        -h: hash of the key, required regardless of how the key is supplied
        -l: minimum allowable time between requests to the source, in seconds

    rm_src    - remove a source
        -s: location of the source

    list_srcs - list the set of sources we can use

    check     - query the list of sources for updates to any of the regularly
                monitored packages
`

var (
	fs         = flag.NewFlagSet("default", flag.ExitOnError)
	pkgName    = fs.String("n", "", "Name of a package")
	pkgVersion = fs.String("v", "", "Version of a package")
	srcUrl     = fs.String("s", "", "The location of a package source")
	rateLimit  = fs.Int("l", 0, "Minimum time between requests to a source, in seconds")
	srcKey     = fs.String("k", "", "Root key for the source, this can be either the key itself or a http[s]:// or file:// URL to the key")
	srcKeyHash = fs.String("h", "", "SHA256 of the key. This is required whether the key is provided directly or by URL")
	blobID     = fs.String("i", "", "Content ID of the blob")
	noWait     = fs.Bool("nowait", false, "Return once installation has started, package will not yet be available.")
	merkle     = fs.String("m", "", "Merkle root of the desired update.")
)

func doTest(pxy *amber.ControlInterface) {
	v := int32(42)
	resp, err := pxy.DoTest(v)
	if err != nil {
		fmt.Println(err)
	} else {
		fmt.Printf("Response: %s\n", resp)
	}
}

func connect(ctx *context.Context) (*amber.ControlInterface, amber.ControlInterfaceRequest) {
	req, pxy, err := amber.NewControlInterfaceRequest()
	if err != nil {
		panic(err)
	}
	ctx.ConnectToEnvService(req)
	return pxy, req
}

func main() {
	if len(os.Args) < 2 {
		fmt.Printf("Error: no command provided\n%s", usage)
		os.Exit(-1)
	}

	fs.Parse(os.Args[2:])

	proxy, _ := connect(context.CreateFromStartupInfo())

	switch os.Args[1] {
	case "get_up":
		// the amber daemon wants package names that start with "/", if not present
		// add this as a prefix
		if strings.Index(*pkgName, "/") != 0 {
			*pkgName = fmt.Sprintf("/%s", *pkgName)
		}
		if *pkgVersion == "" {
			*pkgVersion = "0"
		}
		// combine name and 'version' here, because the FIDL interface is talking
		// about an amber version as opposed to human version. the human version is
		// part of the package name
		*pkgName = fmt.Sprintf("%s/%s", *pkgName, *pkgVersion)
		if *noWait {
			blobID, err := proxy.GetUpdate(*pkgName, nil, merkle)
			if err == nil {
				fmt.Printf("Wrote update to blob %s\n", *blobID)
			} else {
				fmt.Printf("Error getting update %s\n", err)
			}
		} else {
			c, err := proxy.GetUpdateComplete(*pkgName, nil, merkle)
			if err == nil {
				defer c.Close()
				b := make([]byte, 1024)
				daemonErr := false
				for {
					var err error
					var sigs zx.Signals
					sigs, err = zxwait.Wait(*c.Handle(),
						zx.SignalChannelPeerClosed|zx.SignalChannelReadable|ipcserver.ZXSIO_DAEMON_ERROR,
						zx.Sys_deadline_after(zx.Duration((3 * time.Second).Nanoseconds())))

					// If the daemon signaled an error, wait for the error message to
					// become available. daemonErr could be true if the daemon signaled
					// but the read timed out.
					daemonErr = daemonErr ||
						err == nil && sigs&ipcserver.ZXSIO_DAEMON_ERROR == ipcserver.ZXSIO_DAEMON_ERROR
					if daemonErr {
						sigs, err = zxwait.Wait(*c.Handle(),
							zx.SignalChannelPeerClosed|zx.SignalChannelReadable,
							zx.Sys_deadline_after(zx.Duration((3 * time.Second).Nanoseconds())))
					}

					if sigs&zx.SignalChannelReadable == zx.SignalChannelReadable {
						bs, _, err := c.Read(b, []zx.Handle{}, 0)
						if err != nil {
							fmt.Printf("Error reading response from channel: %s\n", err)
						} else if daemonErr {
							fmt.Printf("%s\n", string(b[0:bs]))
						} else {
							fmt.Printf("Wrote update to blob %s\n", string(b[0:bs]))
						}
						break
					}

					if sigs&zx.SignalChannelPeerClosed == zx.SignalChannelPeerClosed {
						fmt.Println("Error: response channel closed unexpectedly.")
						break
					} else if err != nil && err.(zx.Error).Status != zx.ErrTimedOut {
						fmt.Printf("Error awaiting response from channel: %s\n", err)
						break
					} else if err != nil && err.(zx.Error).Status != zx.ErrTimedOut {
						fmt.Printf("Error awaiting response from channel: %s\n", err)
						break
					} else if err != nil && err.(zx.Error).Status == zx.ErrTimedOut {
						fmt.Println("Awaiting response...")
					}
				}
			} else {
				fmt.Printf("Error requesting update: %s\n", err)
			}
		}
	case "get_blob":
		if err := proxy.GetBlob(*blobID); err != nil {
			fmt.Printf("Error getting content blob %s\n", err)
		}
	case "add_src":
		fmt.Printf("%q not yet supported\n", os.Args[1])
	case "rm_src":
		fmt.Printf("%q not yet supported\n", os.Args[1])
	case "list_srcs":
		fmt.Printf("%q not yet supported\n", os.Args[1])
	case "check":
		fmt.Printf("%q not yet supported\n", os.Args[1])
	case "test":
		doTest(proxy)
	default:
		fmt.Printf("Error, %q is not a recognized command\n%s",
			os.Args[1], usage)
		os.Exit(-1)
	}

	proxy.Close()
}
