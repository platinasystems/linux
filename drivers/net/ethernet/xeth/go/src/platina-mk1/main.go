package main

import "xeth"

func main() {
	xeth, err := xeth.New("platina-mk1", stats, xeth.AssertDialOpt(true))
	if err != nil {
		panic(err)
	}
	xeth.Main()
}
