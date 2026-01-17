package com.vtfs.server

import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.runApplication
import org.springframework.context.annotation.ComponentScan

@SpringBootApplication
@ComponentScan(basePackages = ["com.vtfs.server"])
class VtfsApplication

fun main(args: Array<String>) {
    runApplication<VtfsApplication>(*args)
}
