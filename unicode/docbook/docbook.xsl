<?xml version='1.0'?>
<xsl:stylesheet
    xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">

  <xsl:template name="user.head.content">
    <xsl:element name="script" namespace="http://www.w3.org/1999/xhtml">
      <xsl:attribute name="type">text/javascript</xsl:attribute>
      <xsl:attribute name="src">
	<xsl:text>frame.js</xsl:text>
      </xsl:attribute>
      <xsl:text>/* */</xsl:text>
    </xsl:element>

    <xsl:element name="link" namespace="http://www.w3.org/1999/xhtml">
      <xsl:attribute name="rel">icon</xsl:attribute>
      <xsl:attribute name="href">
	<xsl:text>icon.gif</xsl:text>
      </xsl:attribute>
      <xsl:attribute name="type">image/gif</xsl:attribute>
    </xsl:element>
  </xsl:template>

  <xsl:template name="body.attributes">
    <xsl:choose>
      <xsl:when test='@id'>
	<xsl:attribute name="id"><xsl:text>body</xsl:text><xsl:value-of select='@id' /></xsl:attribute>
      </xsl:when>
    </xsl:choose>
  </xsl:template>

  <xsl:param name="funcsynopsis.style">ansi</xsl:param>
  <xsl:param name="toc.section.depth">1</xsl:param>

  <xsl:include href="http://docbook.sourceforge.net/release/xsl/current/xhtml-1_1/chunk.xsl" />

  <!-- Bug fix 1.76.1 -->
  <xsl:template match="funcdef/function" mode="ansi-tabular">
    <xsl:choose>
      <xsl:when test="$funcsynopsis.decoration != 0">
	<strong xmlns="http://www.w3.org/1999/xhtml"
		xmlns:xslo="http://www.w3.org/1999/XSL/Transform"><xsl:apply-templates mode="ansi-nontabular"/></strong>
      </xsl:when>
      <xsl:otherwise>
	<xsl:apply-templates mode="kr-tabular"/>
      </xsl:otherwise>
    </xsl:choose>
  </xsl:template>
</xsl:stylesheet>
