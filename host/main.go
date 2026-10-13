package main

import (
	"errors"
	"flag"
	"fmt"
	"os"
)

func main() {
	cfg, cmd, err := parseArgs(os.Args[1:], os.Stderr)
	if err != nil {
		if errors.Is(err, flag.ErrHelp) {
			return
		}
		fmt.Fprintln(os.Stderr, err)
		os.Exit(2)
	}
	if shouldShowBanner(cfg, cmd) {
		printBanner()
	}
	if err := dispatch(cfg, cmd); err != nil {
		if cmd != "" || !isTerminal() {
			fmt.Fprintln(os.Stderr, err)
		}
		os.Exit(1)
	}
}
