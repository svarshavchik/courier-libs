foo
bar
&amp;
&#20;
a&#20=b
>
pi: [<? xx ? ?>]

comment: [<!-- -- ----->]

sgml comment: [<!DOCTYPE >]

Empty tag: [<span />][< span/ >]

Opening tag: {< SpAn lang>]
Closing tag: [<  / span>]

Opening tag: {< SpAn lang=value>]
Closing tag: [<  / span>]

Opening tag: {< SpAn lang='value'>]
Closing tag: [<  / span>]

Opening tag: {< SpAn lang="value">]
Closing tag: [<  / span>]

Opening tag: {< SpAn DiR='val"ue'>]
Closing tag: [<  / span>]

Bad tags:

<-
<<
>
>

< /foo<bar>

&lt;A&gt; tags:

<base href="http://localhost" />

<div><A HREF="?foo=bar&amp;bar=foo" href="ignore">localhost</a></div>
<div><A HREF="?foo=bar&bar=foo">localhost</a></div>
<div><A HREF="?foo=&quot;bar">localhost</a></div>
<div><a href="https://localhost">localhost</a></div>
<div><a href="mailto:nobody@example.com?subject=foo&to=nobody">nobody@example.com</a></div>
<div><a href="cid:<nobody@example.com>">nobody@example.com</a></div>
<div><a href="nntp:localhost">nobody@example.com</a></div>

Checking nesting (max 128):

<div>
<a><a><a><a><a><a><a><a><a><a><a><a><a><a><a><a>
<a><a><a><a><a><a><a><a><a><a><a><a><a><a><a><a>
<a><a><a><a><a><a><a><a><a><a><a><a><a><a><a><a>
<a><a><a><a><a><a><a><a><a><a><a><a><a><a><a><a>
<a><a><a><a><a><a><a><a><a><a><a><a><a><a><a><a>
<a><a><a><a><a><a><a><a><a><a><a><a><a><a><a><a>
<a><a><a><a><a><a><a><a><a><a><a><a><a><a><a><a>
<a><a><a><a><a><a><a><a><a><a><a><a><a><a><a><a>
<a><a><a><a><a><a><a><a><a><a><a><a><a><a><a><a>
</div>

Checking script discarding:

<script language="javascript">

  <foo>
    <script language="javascript">
    </script>
  </foo>

  <bar>
</script>

<style>foobar</style>Foobar.

Checking blockquote handling

<blockquote>
   <blockquote type="cite">
     cite

     <blockquote>
        <blockquote type="CITE">
          cite
          <blockquote type="CITE">
            cite
            <blockquote type="CITE">
            cite
            </blockquote>
          </blockquote>
        </blockquote>
     </blockquote>
   </blockquote>
</blockquote>

Checking IMG

<img src="http://localhost" alt="Blocked image">

<img src="cid:<internal>" alt="Allowed image">
