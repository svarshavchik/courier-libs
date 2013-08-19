<?xml version='1.0'?>
<xsl:stylesheet
    xmlns:xhtml="http://www.w3.org/1999/xhtml"
    xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">

<!-- Zap empty <a> nodes -->

<xsl:template match="xhtml:a">

  <xsl:copy>
    <xsl:apply-templates select="@*|node()"/>
    <xsl:if test="count(child::node()) = 0">
      <xsl:text> </xsl:text>
    </xsl:if>
  </xsl:copy>

</xsl:template>

<xsl:template match="@*|node()">
  <xsl:copy>
    <xsl:apply-templates select="@*|node()"/>
  </xsl:copy>
</xsl:template>


</xsl:stylesheet>
