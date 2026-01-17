package com.vtfs.server.repository

import com.vtfs.server.model.FileEntry
import org.springframework.data.jpa.repository.JpaRepository
import org.springframework.stereotype.Repository

@Repository
interface FileEntryRepository : JpaRepository<FileEntry, Long> {
    
    fun findByPath(path: String): FileEntry?
    
    fun findByIno(ino: Long): FileEntry?
    
    fun findByParentPath(parentPath: String): List<FileEntry>
    
    fun deleteByPath(path: String)
    
    fun existsByPath(path: String): Boolean
    
    fun findTopByOrderByInoDesc(): FileEntry?
}
