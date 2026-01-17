package com.vtfs.server.service

import com.vtfs.server.common.Result
import com.vtfs.server.model.FileEntry
import com.vtfs.server.repository.FileEntryRepository
import org.springframework.stereotype.Service
import org.springframework.transaction.annotation.Transactional
import java.nio.file.Paths
import java.time.Instant

@Service
@Transactional
class FileSystemService(
    private val repository: FileEntryRepository
) {
    
    companion object {
        private const val ROOT_PATH = "/"
        private const val ROOT_INO = 1000L
        private const val MODE_MASK = 511 // 0o777
    }
    
    init {
        if (!repository.existsByPath(ROOT_PATH)) {
            repository.save(FileEntry(
                path = ROOT_PATH,
                ino = ROOT_INO,
                type = FileEntry.EntryType.DIR,
                mode = MODE_MASK,
                nlink = 2,
                size = 0,
                data = null,
                atime = Instant.now(),
                mtime = Instant.now(),
                ctime = Instant.now(),
                parentPath = null
            ))
        }
    }
    
    private fun normalizePath(path: String): String {
        if (path.isEmpty()) return ROOT_PATH
        val normalized = Paths.get(path).normalize().toString()
        return if (normalized.startsWith("/")) normalized else "/$normalized"
    }
    
    private fun getParentPath(path: String): String {
        if (path == ROOT_PATH) return ROOT_PATH
        val parent = Paths.get(path).parent?.toString() ?: ROOT_PATH
        return if (parent == "/") ROOT_PATH else parent
    }
    
    private fun getFileName(path: String) = Paths.get(path).fileName.toString()
    
    private fun getNextIno() = (repository.findTopByOrderByInoDesc()?.ino ?: ROOT_INO) + 1
    
    private fun findEntry(path: String) = repository.findByPath(normalizePath(path))
    
    private inline fun <T> withEntry(path: String, block: (FileEntry, String) -> Result<T>): Result<T> {
        val normalizedPath = normalizePath(path)
        val entry = repository.findByPath(normalizedPath) ?: return Result.Error("ENOENT")
        return block(entry, normalizedPath)
    }
    
    private inline fun <T> withFile(path: String, block: (FileEntry) -> Result<T>): Result<T> {
        return withEntry(path) { entry, _ ->
            if (entry.type != FileEntry.EntryType.FILE) Result.Error("EISDIR")
            else block(entry)
        }
    }
    
    private inline fun <T> withDir(path: String, block: (FileEntry, String) -> Result<T>): Result<T> {
        return withEntry(path) { entry, normalizedPath ->
            if (entry.type != FileEntry.EntryType.DIR) Result.Error("ENOTDIR")
            else block(entry, normalizedPath)
        }
    }
    
    fun listDir(path: String): Result<List<Map<String, Any>>> {
        return withDir(path) { _, normalizedPath ->
            val children = repository.findByParentPath(normalizedPath).map { child ->
                mapOf(
                    "name" to getFileName(child.path),
                    "ino" to child.ino,
                    "type" to child.type.name.lowercase(),
                    "mode" to child.mode,
                    "size" to child.size
                )
            }
            Result.Success(children)
        }
    }
    
    fun create(path: String, entryType: String, mode: Int): Result<Map<String, Any>> {
        val normalizedPath = normalizePath(path)
        
        if (repository.existsByPath(normalizedPath)) {
            return Result.Error("EEXIST")
        }
        
        val parentPath = getParentPath(normalizedPath)
        val parent = repository.findByPath(parentPath) ?: return Result.Error("ENOENT")
        
        if (parent.type != FileEntry.EntryType.DIR) {
            return Result.Error("ENOTDIR")
        }
        
        val type = when (entryType.lowercase()) {
            "file" -> FileEntry.EntryType.FILE
            "dir" -> FileEntry.EntryType.DIR
            else -> return Result.Error("EINVAL")
        }
        
        val ino = getNextIno()
        val now = Instant.now()
        
        repository.save(FileEntry(
            path = normalizedPath,
            ino = ino,
            type = type,
            mode = mode and MODE_MASK,
            nlink = if (type == FileEntry.EntryType.DIR) 2 else 1,
            size = 0,
            data = null,
            atime = now,
            mtime = now,
            ctime = now,
            parentPath = parentPath
        ))
        
        if (type == FileEntry.EntryType.DIR) {
            parent.nlink++
            parent.mtime = now
            parent.ctime = now
            repository.save(parent)
        }
        
        return Result.Success(mapOf("ino" to ino, "path" to normalizedPath))
    }
    
    fun delete(path: String): Result<Map<String, Any>> {
        val normalizedPath = normalizePath(path)
        
        if (normalizedPath == ROOT_PATH) {
            return Result.Error("EBUSY")
        }
        
        val entry = repository.findByPath(normalizedPath) ?: return Result.Error("ENOENT")
        
        if (entry.type == FileEntry.EntryType.DIR && repository.findByParentPath(normalizedPath).isNotEmpty()) {
            return Result.Error("ENOTEMPTY")
        }
        
        entry.nlink--
        
        if (entry.nlink == 0) {
            val parent = repository.findByPath(entry.parentPath ?: ROOT_PATH)
            
            if (parent != null && entry.type == FileEntry.EntryType.DIR) {
                parent.nlink--
                parent.mtime = Instant.now()
                parent.ctime = Instant.now()
                repository.save(parent)
            }
            
            repository.delete(entry)
        } else {
            entry.ctime = Instant.now()
            repository.save(entry)
        }
        
        return Result.Success(mapOf("deleted" to normalizedPath))
    }
    
    fun read(path: String, offset: Int, size: Int?): Result<ByteArray> {
        return withFile(path) { entry ->
            val data = entry.data ?: return@withFile Result.Success(ByteArray(0))
            
            if (offset >= data.size) {
                return@withFile Result.Success(ByteArray(0))
            }
            
            val end = size?.let { minOf(offset + it, data.size) } ?: data.size
            
            entry.atime = Instant.now()
            repository.save(entry)
            
            Result.Success(data.sliceArray(offset until end))
        }
    }
    
    fun write(path: String, offset: Int, data: ByteArray): Result<Map<String, Any>> {
        return withFile(path) { entry ->
            val currentData = entry.data ?: ByteArray(0)
            val newSize = maxOf(offset + data.size, currentData.size)
            val newData = ByteArray(newSize)
            
            if (offset == 0) {
                data.copyInto(newData)
            } else {
                currentData.copyInto(newData, endIndex = minOf(currentData.size, offset))
                data.copyInto(newData, destinationOffset = offset)
                if (offset < currentData.size) {
                    val remainingStart = offset + data.size
                    if (remainingStart < currentData.size) {
                        currentData.copyInto(newData, destinationOffset = remainingStart, startIndex = remainingStart)
                    }
                }
            }
            
            entry.data = newData
            entry.size = newData.size.toLong()
            entry.mtime = Instant.now()
            entry.ctime = Instant.now()
            repository.save(entry)
            
            Result.Success(mapOf("written" to data.size))
        }
    }
    
    fun stat(path: String): Result<Map<String, Any>> {
        return withEntry(path) { entry, _ ->
            Result.Success(mapOf(
                "ino" to entry.ino,
                "type" to entry.type.name.lowercase(),
                "mode" to entry.mode,
                "nlink" to entry.nlink,
                "size" to entry.size,
                "atime" to entry.atime.epochSecond,
                "mtime" to entry.mtime.epochSecond,
                "ctime" to entry.ctime.epochSecond
            ))
        }
    }
    
    fun link(oldPath: String, newPath: String): Result<Map<String, Any>> {
        val normalizedOldPath = normalizePath(oldPath)
        val normalizedNewPath = normalizePath(newPath)
        
        val oldEntry = repository.findByPath(normalizedOldPath) ?: return Result.Error("ENOENT")
        
        if (oldEntry.type == FileEntry.EntryType.DIR) {
            return Result.Error("EPERM")
        }
        
        if (repository.existsByPath(normalizedNewPath)) {
            return Result.Error("EEXIST")
        }
        
        val parentPath = getParentPath(normalizedNewPath)
        val parent = repository.findByPath(parentPath) ?: return Result.Error("ENOENT")
        
        if (parent.type != FileEntry.EntryType.DIR) {
            return Result.Error("ENOTDIR")
        }
        
        val now = Instant.now()
        
        repository.save(FileEntry(
            path = normalizedNewPath,
            ino = oldEntry.ino,
            type = oldEntry.type,
            mode = oldEntry.mode,
            nlink = oldEntry.nlink + 1,
            size = oldEntry.size,
            data = oldEntry.data,
            atime = now,
            mtime = oldEntry.mtime,
            ctime = now,
            parentPath = parentPath
        ))
        
        oldEntry.nlink++
        oldEntry.ctime = now
        repository.save(oldEntry)
        
        return Result.Success(mapOf("linked" to normalizedNewPath))
    }
}
