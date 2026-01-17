package com.vtfs.server.config

import com.fasterxml.jackson.databind.ObjectMapper
import jakarta.servlet.http.HttpServletRequest
import jakarta.servlet.http.HttpServletResponse
import org.springframework.http.MediaType
import org.springframework.web.servlet.HandlerInterceptor

class TokenInterceptor : HandlerInterceptor {
    
    private val objectMapper = ObjectMapper()
    
    override fun preHandle(request: HttpServletRequest, response: HttpServletResponse, handler: Any): Boolean {
        val token = request.getParameter("token")
        
        if (token.isNullOrEmpty()) {
            response.status = 400
            response.contentType = MediaType.APPLICATION_JSON_VALUE
            response.writer.write(objectMapper.writeValueAsString(mapOf("error" to "EACCES")))
            return false
        }
        
        return true
    }
}
