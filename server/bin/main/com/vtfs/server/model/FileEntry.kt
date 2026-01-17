package com.vtfs.server.model

import jakarta.persistence.*
import java.time.Instant

@Entity
@Table(name = "file_entries")
data class FileEntry(
    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    val id: Long = 0,
    
    @Column(unique = true, length = 512)
    val path: String,
    
    @Column(unique = true)
    val ino: Long,
    
    @Enumerated(EnumType.STRING)
    val type: EntryType,
    
    val mode: Int,
    var nlink: Int = 1,
    var size: Long = 0,
    
    @Lob
    var data: ByteArray? = null,
    
    var atime: Instant = Instant.now(),
    var mtime: Instant = Instant.now(),
    var ctime: Instant = Instant.now(),
    
    @Column(length = 512)
    val parentPath: String? = null
) {
    enum class EntryType { FILE, DIR }
    
    override fun equals(other: Any?) = other is FileEntry && id == other.id
    override fun hashCode() = id.hashCode()
}
