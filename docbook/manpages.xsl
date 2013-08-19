<?xml version='1.0'?>
<xsl:stylesheet  
    xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">

<xsl:include href="/usr/share/sgml/docbook/xsl-stylesheets/xhtml/onechunk.xsl"/>

<xsl:param name="html.stylesheet" select="'style.css'"/>
<xsl:param name="admon.graphics" select="0"/>

<xsl:param name="use.id.as.filename" select="1"/>

<xsl:param name="funcsynopsis.style">ansi</xsl:param>

<xsl:param name="table.borders.with.css" select="1" />

<xsl:param name="default.table.frame" select="'collapse'" />
<xsl:param name="table.cell.border.style" select="''" />
<xsl:param name="table.cell.border.thickness" select="''" />
<xsl:param name="table.cell.border.color" select="''" />
<xsl:param name="emphasis.propagates.style" select="1" />
<xsl:param name="para.propagates.style" select="1" />
<xsl:param name="entry.propagates.style" select="1" />

<xsl:param name="part.autolabel" select="0" />
<xsl:param name="section.autolabel" select="0" />
<xsl:param name="chapter.autolabel" select="0" />

<xsl:template name="user.head.content">

   <link rel='stylesheet' type='text/css' href='manpage.css' />
   <meta name="MSSmartTagsPreventParsing" content="TRUE" />
   <link rel="icon" href="icon.gif" type="image/gif" />
    <xsl:comment>

Copyright 1998 - 2009 Double Precision, Inc.  See COPYING for distribution
information.

</xsl:comment>
</xsl:template>

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

