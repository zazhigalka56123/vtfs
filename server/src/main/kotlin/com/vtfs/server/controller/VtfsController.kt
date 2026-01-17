package com.vtfs.server.controller

import com.vtfs.server.common.Result
import com.vtfs.server.service.FileSystemService
import org.springframework.http.ResponseEntity
import org.springframework.web.bind.annotation.*
import java.util.Base64

@RestController
class VtfsController(
    private val fileSystemService: FileSystemService
) {
    
    private fun <T> Result<T>.toResponse(): ResponseEntity<Map<String, Any>> = when (this) {
        is Result.Success -> ResponseEntity.ok(mapOf("result" to (data ?: emptyMap<Any, Any>())))
        is Result.Error -> ResponseEntity.status(400).body(mapOf("error" to code))
    }
    
    @GetMapping("/list")
    fun list(@RequestParam(defaultValue = "/") path: String) = 
        fileSystemService.listDir(path).toResponse()
    
    @GetMapping("/create")
    fun create(
        @RequestParam path: String,
        @RequestParam(defaultValue = "file") type: String,
        @RequestParam(defaultValue = "777") mode: String
    ): ResponseEntity<Map<String, Any>> {
        val modeInt = mode.toIntOrNull(8) ?: return Result.Error("EINVAL").toResponse()
        return fileSystemService.create(path, type, modeInt).toResponse()
    }
    
    @GetMapping("/delete")
    fun delete(@RequestParam path: String) = 
        fileSystemService.delete(path).toResponse()
    
    @GetMapping("/read")
    fun read(
        @RequestParam path: String,
        @RequestParam(defaultValue = "0") offset: Int,
        @RequestParam(required = false) size: Int?
    ): ResponseEntity<Map<String, Any>> {
        return when (val result = fileSystemService.read(path, offset, size)) {
            is Result.Success -> {
                val base64Data = Base64.getEncoder().encodeToString(result.data)
                ResponseEntity.ok(mapOf("result" to mapOf("data" to base64Data)))
            }
            is Result.Error -> ResponseEntity.status(400).body(mapOf("error" to result.code))
        }
    }
    
    @GetMapping("/write")
    fun write(
        @RequestParam path: String,
        @RequestParam(defaultValue = "0") offset: Int,
        @RequestParam data: String
    ): ResponseEntity<Map<String, Any>> {
        val decodedData = try {
            Base64.getDecoder().decode(data)
        } catch (e: IllegalArgumentException) {
            return Result.Error("EINVAL").toResponse()
        }
        return fileSystemService.write(path, offset, decodedData).toResponse()
    }
    
    @GetMapping("/stat")
    fun stat(@RequestParam path: String) = 
        fileSystemService.stat(path).toResponse()
    
    @GetMapping("/link")
    fun link(
        @RequestParam oldpath: String,
        @RequestParam newpath: String
    ) = fileSystemService.link(oldpath, newpath).toResponse()
}
